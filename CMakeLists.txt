cmake_minimum_required(VERSION 3.11...3.21)
project(thinger_monitor)

cmake_policy(SET CMP0135 NEW)

# set package version
set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)

# add version for source code
add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

if(NOT BUILD_VERSION)
  add_definitions( -DBUILD_VERSION="develop" )
  message(STATUS "The program version is develop")
else()
  add_definitions( -DBUILD_VERSION=${BUILD_VERSION} )
  message(STATUS "The program version is ${BUILD_VERSION}")
endif()

# check c++20 support
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(COMPILER_SUPPORTS_CXX20)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++20 support. Please use a different C++ compiler.")
  return()
endif()

# configure Thinger.io parameters
OPTION(ENABLE_OPENSSL "Enable use of OpenSSL" ON)
OPTION(CROSS "Enable cross compilation and static linking" OFF)
OPTION(BIN_EXTENSION "Set binary file extension" OFF)
OPTION(ASIO_CLIENT "Enable ASIO CLIENT" ON)

set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_LIBS        ON)
find_package(Boost REQUIRED COMPONENTS system thread regex program_options date_time filesystem)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  list(APPEND ADDITIONAL_LIBS ${Boost_LIBRARIES})
endif()

# required for GCC < 9.0
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  list(APPEND ADDITIONAL_LIBS "stdc++fs")
endif()

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
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -Wno-psabi")
endif()

set(SOURCE_FILES src/main.cpp)

include(FetchContent)

# fmt
#find_package(fmt)
#if(fmt_FOUND)
#  include_directories(${fmt_INCLUDE_DIRS})
#  list(APPEND ADDITIONAL_LIBS fmt::fmt)
#endif()
# fmt
FetchContent_Declare(fmt URL https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip)
FetchContent_MakeAvailable(fmt)
list(APPEND ADDITIONAL_LIBS fmt::fmt)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY  https://github.com/gabime/spdlog.git
  GIT_TAG         v1.11.0
)
FetchContent_MakeAvailable(spdlog)

# json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)
list(APPEND ADDITIONAL_LIBS nlohmann_json::nlohmann_json)

# httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY  https://github.com/yhirose/cpp-httplib.git
  GIT_TAG         v0.12.0
)
FetchContent_MakeAvailable(httplib)
list(APPEND ADDITIONAL_LIBS httplib::httplib)

FetchContent_Declare(
  thinger
  GIT_REPOSITORY https://github.com/thinger-io/IOTMP-Linux.git
  #add_definitions(-DTHINGER_SERVER="iot.thinger.io")
  #add_definitions(-DTHINGER_ENABLE_STREAM_LISTENER)
  GIT_TAG origin/develop
)
FetchContent_MakeAvailable(thinger)
list(APPEND ADDITIONAL_LIBS thinger)

# libarchive
find_package(LibArchive REQUIRED)
if (LibArchive_FOUND)
  include_directories(${LibArchive_INCLUDE_DIRS})
  list(APPEND ADDITIONAL_LIBS LibArchive::LibArchive)
  message(STATUS "LibArchive Version: ${LibArchive_VERSION}")
endif()

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} ${ADDITIONAL_LIBS})
#set_target_properties(thinger_monitor PROPERTIES COMPILE_DEFINITIONS "DAEMON=0")

# Export the thinger library for use in other projects
#set(EXPORTED_TARGETS)
export(TARGETS nlohmann_json
       NAMESPACE nlohmann_json::
       FILE ${CMAKE_CURRENT_BINARY_DIR}/monitoringClientTargets.cmake)

export(TARGETS httplib
  NAMESPACE httplib::
  FILE ${CMAKE_CURRENT_BINARY_DIR}/httplibTargets.cmake)

export(TARGETS fmt
  NAMESPACE fmt::
  FILE ${CMAKE_CURRENT_BINARY_DIR}/fmtTargets.cmake)

export(TARGETS spdlog
  NAMESPACE spdlog::
  FILE ${CMAKE_CURRENT_BINARY_DIR}/spdlogTargets.cmake)

# Catch2
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY  https://github.com/catchorg/Catch2.git
  GIT_TAG         v3.1.0
)
FetchContent_MakeAvailable(Catch2)
add_executable(tests test/thinger/config.cpp)
#target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
list(APPEND ADDITIONAL_LIBS Catch2::Catch2WithMain)
target_link_libraries(tests ${ADDITIONAL_LIBS})

if(BIN_EXTENSION)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.${BIN_EXTENSION}")
  message(STATUS "Binary name is ${PROJECT_NAME}.${BIN_EXTENSION}")
endif()
