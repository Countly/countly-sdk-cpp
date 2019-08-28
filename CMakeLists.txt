cmake_minimum_required(VERSION 3.0)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scripts/version.txt COUNTLY_SDK_VERSION)
string(STRIP ${COUNTLY_SDK_VERSION} COUNTLY_SDK_VERSION)

project(countly-sdk VERSION ${COUNTLY_SDK_VERSION} LANGUAGES CXX)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/countly.hpp.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/countly.hpp")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB COUNTLY_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(countly STATIC ${COUNTLY_SOURCE_FILES})
