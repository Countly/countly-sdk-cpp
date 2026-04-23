cmake_minimum_required(VERSION 3.16)
project(qt_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(CURL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)

# Countly SDK paths — defaults to the parent SDK checkout (this demo lives in
# countly-sdk-cpp/examples/qt_demo/). Override with -DCOUNTLY_SDK_DIR=... if
# you keep the SDK somewhere else.
if(NOT COUNTLY_SDK_DIR)
    get_filename_component(COUNTLY_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
set(COUNTLY_BUILD_DIR "${COUNTLY_SDK_DIR}/build")

add_executable(qt_demo main.cpp)
target_compile_definitions(qt_demo PRIVATE COUNTLY_USE_SQLITE)
target_include_directories(qt_demo PRIVATE
    ${COUNTLY_SDK_DIR}/include
    ${COUNTLY_SDK_DIR}/vendor/json/include
)
target_link_libraries(qt_demo PRIVATE
    CURL::libcurl
    Qt6::Widgets
    Qt6::WebEngineWidgets
    ${COUNTLY_BUILD_DIR}/libcountly.dylib
)
