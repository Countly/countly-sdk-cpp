cmake_minimum_required(VERSION 3.13...3.31)

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
option(COUNTLY_USE_SYSTEM_SQLITE "Link the system SQLite instead of compiling the vendored amalgamation" OFF)
option(COUNTLY_USE_SYSTEM_JSON "Compile against the system nlohmann_json instead of the vendored copy" OFF)
option(COUNTLY_BUILD_TESTS "Build test programs" OFF)
option(COUNTLY_BUILD_SAMPLE "Build Sample programs" OFF)

message("Build shared libraries:" ${BUILD_SHARED_LIBS})
message("Create a module definition (.def) on Windows.:" ${CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS})
message("Use a custom HTTP library:" ${COUNTLY_USE_CUSTOM_HTTP})
message("Use a custom SHA 256 library:" ${COUNTLY_USE_CUSTOM_SHA256})
message("Use SQLite:" ${COUNTLY_USE_SQLITE})
message("Use system SQLite:" ${COUNTLY_USE_SYSTEM_SQLITE})
message("Use system nlohmann_json:" ${COUNTLY_USE_SYSTEM_JSON})
message("Build test programs:" ${COUNTLY_BUILD_TESTS})
message("Build Sample programs:" ${COUNTLY_BUILD_SAMPLE})

# Compile countly public headers: everything countly.hpp includes, so an
# installed copy of the SDK is self-contained. countly.hpp itself is installed
# separately into the include root (see the install rules at the bottom).
set(COUNTLY_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/configuration_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/configuration_provider.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/constants.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/countly_configuration.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/crash_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/event.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/logger_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/remote_config_store.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/request_builder.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/request_module.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/storage_module_base.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly/views_module.hpp)

add_library(countly
  ${COUNTLY_PUBLIC_HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/countly.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/views_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logger_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/request_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crash_module.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/request_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/storage_module_db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/storage_module_memory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/event.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/configuration_module.cpp)

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

# nlohmann/json is part of the public API, so its headers are a usage
# requirement of the countly target. By default the vendored copy is used and
# later installed next to the SDK headers, so the installed tree is
# self-contained. An application that already uses nlohmann/json should set
# COUNTLY_USE_SYSTEM_JSON to compile the SDK against that same copy instead,
# so only one version of the library exists in the build (the SDK headers
# reject unsupported versions at compile time).
if(COUNTLY_USE_SYSTEM_JSON)
  find_package(nlohmann_json 3.11 REQUIRED)
  target_link_libraries(countly nlohmann_json::nlohmann_json)
else()
  target_include_directories(countly PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include>
    $<INSTALL_INTERFACE:include>)
endif()
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
  execute_process(COMMAND brew --prefix openssl OUTPUT_VARIABLE OPENSSL_ROOT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
  endif()
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
  target_compile_definitions(countly PRIVATE COUNTLY_USE_SQLITE)
  if(COUNTLY_USE_SYSTEM_SQLITE)
    message("Linking system SQLite")
    find_package(SQLite3 REQUIRED)
    target_link_libraries(countly SQLite::SQLite3)
  else()
    message("Compiling vendored SQLite amalgamation")
    enable_language(C)
    target_sources(countly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3/sqlite3.c)
    target_include_directories(countly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3)
    # No loadable-extension support: not needed by the SDK, avoids linking
    # libdl and removes the extension attack surface.
    target_compile_definitions(countly PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_MEMSTATUS=0)
    if(NOT WIN32)
      target_link_libraries(countly m)
    endif()
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
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/immediate_stop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/mutex_exception_safety.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/sbs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/internal_limits.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/internal_limits_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/multi_instance.cpp)
    
    target_compile_options(countly-tests PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g>)
    target_compile_definitions(countly-tests PRIVATE COUNTLY_BUILD_TESTS)
  if(COUNTLY_USE_SQLITE)
    message("Compiling definitions for sqlite")
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_SQLITE)
    if(COUNTLY_USE_SYSTEM_SQLITE)
      target_link_libraries(countly-tests SQLite::SQLite3)
    else()
      target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3)
    endif()
  endif()
  if(COUNTLY_USE_CUSTOM_SHA256)
    message("Compiling definitions for custom sha256")
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_CUSTOM_SHA256)
  endif()
  target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  # nlohmann/json comes in as a usage requirement of the countly target.
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
  if(COUNTLY_USE_SYSTEM_SQLITE)
    target_link_libraries(countly-sample SQLite::SQLite3)
  else()
    target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3)
  endif()
endif()
  target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  # nlohmann/json comes in as a usage requirement of the countly target.
  target_link_libraries(countly-sample countly)
  set_target_properties(countly-sample PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()

install(TARGETS countly
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/countly)
# countly.hpp lives in the include root, so `#include "countly.hpp"` works
# against the installed tree just like against the source tree.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp DESTINATION include)
# The SDK headers include "nlohmann/json.hpp", so ship the vendored copy with
# them; without it an installed tree does not compile on its own. Skipped when
# building against the system copy, which the application provides itself.
if(NOT COUNTLY_USE_SYSTEM_JSON)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include/nlohmann DESTINATION include)
endif()
