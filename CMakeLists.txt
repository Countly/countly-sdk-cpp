cmake_minimum_required(VERSION 3.0)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scripts/version.txt COUNTLY_SDK_VERSION)
string(STRIP ${COUNTLY_SDK_VERSION} COUNTLY_SDK_VERSION)

project(libcountly VERSION ${COUNTLY_SDK_VERSION} LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(COUNTLY_USE_STDLIB "Use the C++11 standard library" ON)
option(COUNTLY_USE_CUSTOM_HTTP "Use a custom HTTP library" OFF)
option(COUNTLY_BUILD_TESTS "Build test programs" OFF)

if (NOT BUILD_SHARED_LIBS AND NOT COUNTLY_CUSTOM_HTTP)
  message(FATAL_ERROR "You must provide a custom HTTP library when compiling statically.")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/countly.hpp"
  COPYONLY)

add_library(countly src/countly.cpp)

target_include_directories(countly
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(countly PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

if(COUNTLY_USE_STDLIB)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_STDLIB)
  # else use a special compiler
endif()

if(COUNTLY_USE_CUSTOM_HTTP)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_HTTP)
# else if not windows; link curl
endif()

if(COUNTLY_BUILD_TESTS)
  find_package(doctest REQUIRED)
  add_executable(countly-tests)
  target_link_libraries(countly-tests countly doctest::doctest)
endif()
