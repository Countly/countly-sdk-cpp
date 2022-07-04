cmake_minimum_required(VERSION 3.0)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp COUNTLY_HPP_CONTENTS)
string(REGEX MATCH "#define COUNTLY_SDK_VERSION \"[^\"]+\"" COUNTLY_SDK_VERSION ${COUNTLY_HPP_CONTENTS})
string(REGEX REPLACE "#define COUNTLY_SDK_VERSION \"([^\"]+)\"" "\\1" COUNTLY_SDK_VERSION ${COUNTLY_SDK_VERSION})

project(countly VERSION ${COUNTLY_SDK_VERSION} LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS  "Create a module definition (.def) on Windows." ON)
option(COUNTLY_USE_CUSTOM_HTTP "Use a custom HTTP library" OFF)
option(COUNTLY_USE_CUSTOM_SHA256 "Use a custom SHA 256 library" OFF)
option(COUNTLY_USE_SQLITE "Use SQLite" OFF)
option(COUNTLY_BUILD_TESTS "Build test programs" OFF)
option(COUNTLY_BUILD_SAMPLE "Build Sample programs" OFF)

if (NOT WIN32 AND NOT BUILD_SHARED_LIBS AND NOT COUNTLY_USE_CUSTOM_HTTP)
  message(FATAL_ERROR "You must provide a custom HTTP function when compiling statically.")
endif()

add_library(countly
  ${CMAKE_CURRENT_SOURCE_DIR}/src/countly.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/event.cpp)

target_include_directories(countly
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(countly PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

find_package(Threads)
target_link_libraries(countly Threads::Threads)

target_include_directories(countly PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)

if (COUNTLY_USE_CUSTOM_SHA256)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_SHA256)
else()
if (APPLE)
  set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()
find_package(OpenSSL REQUIRED)
target_include_directories(countly PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(countly ${OPENSSL_LIBRARIES})
endif()

if(COUNTLY_USE_CUSTOM_HTTP)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_HTTP)
elseif(NOT WIN32)
  find_package(CURL REQUIRED)
  target_include_directories(countly PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(countly ${CURL_LIBRARIES})
endif()

if(COUNTLY_USE_SQLITE)
  target_compile_definitions(countly PRIVATE COUNTLY_USE_SQLITE)
  set(Sqlite3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build)
  find_package(Sqlite3)
  target_include_directories(countly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  target_link_libraries(countly sqlite3)
endif()

if(COUNTLY_BUILD_TESTS)
  add_executable(countly-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/event.cpp)
  if(COUNTLY_USE_SQLITE)
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_SQLITE)
    target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  endif()
  target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  target_include_directories(countly-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)
  target_link_libraries(countly-tests countly)
  set_target_properties(countly-tests PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()

if(COUNTLY_BUILD_SAMPLE)
  add_executable(countly-sample
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/example_integration.cpp)
  if(COUNTLY_USE_SQLITE)
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_SQLITE)
    target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  endif()
  target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  target_include_directories(countly-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)
  target_link_libraries(countly-sample countly)
  set_target_properties(countly-sample PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()
