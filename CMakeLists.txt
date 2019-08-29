cmake_minimum_required(VERSION 3.0)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scripts/version.txt COUNTLY_SDK_VERSION)
string(STRIP ${COUNTLY_SDK_VERSION} COUNTLY_SDK_VERSION)

project(libcountly VERSION ${COUNTLY_SDK_VERSION} LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(COUNTLY_USE_CUSTOM_HTTP "Use a custom HTTP library" OFF)
option(COUNTLY_USE_SQLITE "Use SQLite" ON)
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

if(COUNTLY_USE_CUSTOM_HTTP)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_HTTP)
elseif(NOT WIN32)
  find_package(CURL REQUIRED)
  target_link_libraries(countly CURL::libcurl)
endif()

if(COUNTLY_USE_SQLITE)
  find_package(Threads)
  target_link_libraries(countly Threads::Threads)
  set(Sqlite3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build")
  find_package(Sqlite3)
  target_link_libraries(countly sqlite3)
endif()

if(COUNTLY_BUILD_TESTS)
  find_package(doctest REQUIRED)
  add_executable(countly-tests)
  target_link_libraries(countly-tests countly doctest::doctest)
endif()
