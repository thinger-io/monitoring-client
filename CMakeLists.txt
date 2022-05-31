cmake_minimum_required(VERSION 3.16.0)
project(thinger_monitor)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

if(NOT BUILD_VERSION)
    SET(BUILD_VERSION "Not set")
else()
    add_definitions( -DBUILD_VERSION=${BUILD_VERSION} )
    message(STATUS "The program version is ${BUILD_VERSION}")
endif()

# check c++17 or c++20 support
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(COMPILER_SUPPORTS_CXX20)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
elseif(COMPILER_SUPPORTS_CXX17)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler.")
endif()

OPTION(ENABLE_OPENSSL "Enable use of OpenSSL" ON)
OPTION(CROSS "Enable cross compilation and static linkin" OFF)
OPTION(BIN_EXTENSION "Set binary file extension" OFF)

# OpenSSL
if(ENABLE_OPENSSL)
    SET(OPEN_SSL 1)
    if(CROSS)
      set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif()
    find_package(OpenSSL REQUIRED)
    if(OPENSSL_FOUND)
        include_directories(${OPENSSL_INCLUDE_DIR})
        list(APPEND ADDITIONAL_LIBS OpenSSL::SSL)
        list(APPEND ADDITIONAL_LIBS OpenSSL::Crypto)
        message(STATUS "OpenSSL Version: ${OPENSSL_VERSION} ${OPENSSL_INCLUDE_DIR} ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES}")
    endif()
else()
    SET(OPEN_SSL 0)
endif()

if(CROSS)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

  # generate a static binary
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")

  # strip binary for smaller size
  if (NOT CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
  else()
      message(STATUS "Debug mode")
  endif()
else()
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
endif()

set(SOURCE_FILES src/main.cpp)

# set OpenSSL if available
add_definitions( -DOPEN_SSL=${OPEN_SSL} )

# json
include(FetchContent)
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.10.3
)

FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
  add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
  list(APPEND ADDITIONAL_LIBS nlohmann_json::nlohmann_json)
endif()

find_package(LibArchive REQUIRED)
if (LibArchive_FOUND)
  include_directories(${LibArchive_INCLUDE_DIRS})
  list(APPEND ADDITIONAL_LIBS LibArchive::LibArchive)
  message(STATUS "LibArchive Version: ${LibArchive_VERSION}")
endif()

add_executable(thinger_monitor ${SOURCE_FILES})
target_link_libraries(thinger_monitor ${ADDITIONAL_LIBS})
set_target_properties(thinger_monitor PROPERTIES COMPILE_DEFINITIONS "DAEMON=0")

if(BIN_EXTENSION)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.${BIN_EXTENSION}")
  message(STATUS "Binary name is ${PROJECT_NAME}.${BIN_EXTENSION}")
endif()


# Add thinger libraries
FetchContent_Declare(
  thinger
  GIT_REPOSITORY https://github.com/thinger-io/Linux-Client.git
  GIT_TAG d802aa3401bfdd308176e53907b6ccedc1bf0a9f # the commit hash for 2021-10-26
)
FetchContent_MakeAvailable(thinger)
target_include_directories(thinger_monitor PRIVATE ${thinger_SOURCE_DIR}/src/thinger )

# Add cpp_httplib library
# Added localy as an external library until it implements unix sockets
#FetchContent_Declare(
#  cpp_httplib
#  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
#  GIT_TAG v0.10.1
#)
#FetchContent_MakeAvailable(cpp_httplib)
#target_include_directories(thinger_monitor PRIVATE ${cpp_httplib_SOURCE_DIR})
target_include_directories(thinger_monitor PRIVATE ${CMAKE_SOURCE_DIR}/external/cpp-httplib/)
