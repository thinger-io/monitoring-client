cmake_minimum_required(VERSION 2.8.13)
project(thinger_monitor)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

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
OPTION(DAEMON "Build thinger client as daemon" OFF)
OPTION(EDISON "Enable build and install for Intel Edison" OFF)
OPTION(RASPBERRY "Enable build and isntall for Raspberry Pi" OFF)

# Check for jsoncpp
#find_package(JsonCpp REQUIRED)
#if (JsonCpp_FOUND)
#  list(APPEND ADDITIONAL_LIBS jsoncpp)
#else()
#  message(FATAL_ERROR "Please install jsoncpp before proceeding.")
#endif()

# Find OpenSSL
IF(ENABLE_OPENSSL)
  if(APPLE) # point to the correct OpenSSL path (installed by homebrew)
      SET(OPEN_SSL 1)
      include_directories(/usr/local/opt/openssl/include)
      list(APPEND ADDITIONAL_LIBS /usr/local/opt/openssl/lib/libssl.a)
      list(APPEND ADDITIONAL_LIBS /usr/local/opt/openssl/lib/libcrypto.a)
  else()
      find_package(OpenSSL REQUIRED)
      if(OPENSSL_FOUND)
          SET(OPEN_SSL 1)
          include_directories(${OPENSSL_INCLUDE_DIR})
          list(APPEND ADDITIONAL_LIBS ${OPENSSL_LIBRARIES})
          message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
      else()
          SET(OPEN_SSL 0)
      endif()
  endif()
ELSE()
  SET(OPEN_SSL 0)
ENDIF()

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

if(DAEMON)
    # daemon service that can be installed as a linux service
    add_executable(thinger_monitor ${SOURCE_FILES})
    target_link_libraries(thinger_monitor ${ADDITIONAL_LIBS})
    set_target_properties(thinger_monitor PROPERTIES COMPILE_DEFINITIONS "DAEMON=1")

    if(EDISON)
        install(TARGETS thinger_monitor RUNTIME DESTINATION bin)
        install(FILES "${CMAKE_SOURCE_DIR}/install/intel_edison/thinger.service" DESTINATION /lib/systemd/system)
    elseif(RASPBERRY)
        install(TARGETS thinger_monitor RUNTIME DESTINATION bin)
        install(FILES "${CMAKE_SOURCE_DIR}/install/raspberry/thinger" DESTINATION /etc/init.d)
    elseif(MAC)

    endif()
else()
    add_executable(thinger_monitor ${SOURCE_FILES})
    target_link_libraries(thinger_monitor ${ADDITIONAL_LIBS})
    set_target_properties(thinger_monitor PROPERTIES COMPILE_DEFINITIONS "DAEMON=0")
endif()

# Add thinger libraries
FetchContent_Declare(
  thinger
  GIT_REPOSITORY https://github.com/thinger-io/Linux-Client.git
  GIT_TAG 7626d1664d9488f302a9163aa20c4345f6a065c7 # the commit hash for 2021-09-15
)
FetchContent_MakeAvailable(thinger)
target_include_directories(thinger_monitor PRIVATE ${thinger_SOURCE_DIR}/src/thinger )

# Add cpp_httplib library
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG d87d0672a8e0f3695f168ff1f55028f6fbe4aedf # the commit hash for 1.9.4
)
FetchContent_MakeAvailable(cpp_httplib)
target_include_directories(thinger_monitor PRIVATE ${cpp_httplib_SOURCE_DIR})

#target_link_libraries(thinger_monitor PRIVATE nlohmann_json::nlohmann_json)

