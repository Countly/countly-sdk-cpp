cmake_minimum_required(VERSION 3.0)

# Acquire countly version from constant
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/constants.hpp COUNTLY_HPP_CONTENTS)
string(REGEX MATCH "#define COUNTLY_SDK_VERSION \"[^\"]+\"" COUNTLY_SDK_VERSION ${COUNTLY_HPP_CONTENTS})
string(REGEX REPLACE "#define COUNTLY_SDK_VERSION \"([^\"]+)\"" "\\1" COUNTLY_SDK_VERSION ${COUNTLY_SDK_VERSION})

project(countly VERSION ${COUNTLY_SDK_VERSION} LANGUAGES CXX)

# setup configuration options and their default values
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS  "Create a module definition (.def) on Windows." ON)
option(COUNTLY_USE_CUSTOM_HTTP "Use a custom HTTP library" OFF)
option(COUNTLY_USE_CUSTOM_SHA256 "Use a custom SHA 256 library" OFF)
option(COUNTLY_USE_SQLITE "Use SQLite" OFF)
option(COUNTLY_BUILD_TESTS "Build test programs" OFF)
option(COUNTLY_BUILD_SAMPLE "Build Sample programs" OFF)

message("Build shared libraries:" ${BUILD_SHARED_LIBS})
message("Create a module definition (.def) on Windows.:" ${CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS})
message("Use a custom HTTP library:" ${COUNTLY_USE_CUSTOM_HTTP})
message("Use a custom SHA 256 library:" ${COUNTLY_USE_CUSTOM_SHA256})
message("Use SQLite:" ${COUNTLY_USE_SQLITE})
message("Build test programs:" ${COUNTLY_BUILD_TESTS})
message("Build Sample programs:" ${COUNTLY_BUILD_SAMPLE})

if (NOT WIN32 AND NOT BUILD_SHARED_LIBS AND NOT COUNTLY_USE_CUSTOM_HTTP)
  message(FATAL_ERROR "You must provide a custom HTTP function when compiling statically.")
endif()

# Compile countly public headers
set(COUNTLY_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/constants.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/event.hpp

  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/logger_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/crash_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/views_module.hpp)

add_library(countly
  ${COUNTLY_PUBLIC_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/countly.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/views_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logger_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/request_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crash_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/request_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/storage_module_db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/storage_module_memory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/event.cpp)

target_include_directories(countly
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(countly PROPERTIES
    PUBLIC_HEADER "${COUNTLY_PUBLIC_HEADERS}"
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

find_package(Threads)
target_link_libraries(countly Threads::Threads)

target_include_directories(countly PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)
if (COUNTLY_BUILD_TESTS)
  message("Compiling definitions for tests")
  target_compile_definitions(countly PRIVATE COUNTLY_BUILD_TESTS)
endif()

if (COUNTLY_USE_CUSTOM_SHA256)
  message("Compiling definitions for custom sha256")
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_SHA256)
else()
if (APPLE)
  message("Setting openssl root for Mac")
  set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()
find_package(OpenSSL REQUIRED)
target_include_directories(countly PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(countly ${OPENSSL_LIBRARIES})
endif()

if(COUNTLY_USE_CUSTOM_HTTP)
  message("Will compile definitions for custom http")
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_HTTP)
elseif(NOT WIN32)
  message("Checking url as device is not windows")
  find_package(CURL REQUIRED)
  target_include_directories(countly PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(countly ${CURL_LIBRARIES})
endif()

if(COUNTLY_USE_SQLITE)
  message("Building sqlite directories")
  target_compile_definitions(countly PRIVATE COUNTLY_USE_SQLITE)
  set(Sqlite3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build)
  target_include_directories(countly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  # feel free to change this to a static library that was built in your build file
  if(WIN32)
        target_link_libraries(countly ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build/libsqlite3.dll)
  elseif(APPLE)
      target_link_libraries(countly ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build/libsqlite3.dylib)
  else()
      target_link_libraries(countly ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/build/libsqlite3.so)
  endif()
endif()

if(COUNTLY_BUILD_TESTS)
  message("Building test directories")
  add_executable(countly-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/views.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/event_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/storage_db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/event.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/crash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/request.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config.cpp)
    
    target_compile_options(countly-tests PRIVATE -g)
    target_compile_definitions(countly-tests PRIVATE COUNTLY_BUILD_TESTS)
  if(COUNTLY_USE_SQLITE)
    message("Compiling definitions for sqlite")
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_SQLITE)
    target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  endif()
  if(WIN32)
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lwinhttp")
  endif()
  if(COUNTLY_USE_CUSTOM_SHA256)
    message("Compiling definitions for custom sha256")
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_CUSTOM_SHA256)
  endif()
  target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  target_include_directories(countly-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)
  target_link_libraries(countly-tests countly)
  set_target_properties(countly-tests PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()

if(COUNTLY_BUILD_SAMPLE)
  message("Building sample directories")
  add_executable(countly-sample
  ${CMAKE_CURRENT_SOURCE_DIR}/examples/example_integration.cpp)
  
  if(COUNTLY_USE_SQLITE)
    message("Compiling definitions for sqlite")
    target_compile_definitions(countly-sample PRIVATE COUNTLY_USE_SQLITE)
    target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
  endif()
  if(WIN32)
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lwinhttp")
  endif()
  target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  target_include_directories(countly-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include)
  target_link_libraries(countly-sample countly)
  set_target_properties(countly-sample PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()

install(TARGETS countly ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/countly)