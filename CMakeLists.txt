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

# Public headers installed under include/countly; countly.hpp goes to the include root, see the install rules
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
add_library(countly::countly ALIAS countly)

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

# nlohmann/json is part of the public API, so it is a usage requirement of the countly target
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
if (APPLE AND NOT OPENSSL_ROOT_DIR)
  message("Setting openssl root for Mac")
  execute_process(COMMAND brew --prefix openssl OUTPUT_VARIABLE OPENSSL_ROOT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
  endif()
endif()
find_package(OpenSSL REQUIRED)
if(TARGET OpenSSL::SSL)
  target_link_libraries(countly OpenSSL::SSL OpenSSL::Crypto)
else()
  target_link_libraries(countly OpenSSL::Crypto)
endif()
endif()

if(COUNTLY_USE_CUSTOM_HTTP)
  message("Will compile definitions for custom http")
  target_compile_definitions(countly PRIVATE COUNTLY_USE_CUSTOM_HTTP)
elseif(NOT WIN32)
  message("Checking url as device is not windows")
  find_package(CURL REQUIRED)
  target_link_libraries(countly CURL::libcurl)
endif()

if(COUNTLY_USE_SQLITE)
  # The public headers change with this definition, so code that includes them must see it too
  target_compile_definitions(countly PUBLIC COUNTLY_USE_SQLITE)
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
  if(COUNTLY_USE_CUSTOM_HTTP)
    message("Compiling definitions for custom http")
    target_compile_definitions(countly-tests PRIVATE COUNTLY_USE_CUSTOM_HTTP)
  endif()
  target_include_directories(countly-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
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
  if(COUNTLY_USE_SYSTEM_SQLITE)
    target_link_libraries(countly-sample SQLite::SQLite3)
  else()
    target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3)
  endif()
endif()
  target_include_directories(countly-sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/doctest/doctest)
  target_link_libraries(countly-sample countly)
  set_target_properties(countly-sample PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
endif()

install(TARGETS countly
  EXPORT countlyTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/countly
  INCLUDES DESTINATION include)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/countly.hpp DESTINATION include)
# The installed headers include nlohmann/json.hpp, so the bundled copy ships with them
if(NOT COUNTLY_USE_SYSTEM_JSON)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include/nlohmann DESTINATION include)
endif()

# The package config looks up only the dependencies this build links
set(COUNTLY_CONFIG_DEPENDENCIES "find_dependency(Threads)")
if(COUNTLY_USE_SYSTEM_JSON)
  string(APPEND COUNTLY_CONFIG_DEPENDENCIES "\nfind_dependency(nlohmann_json 3.11)")
endif()
if(NOT COUNTLY_USE_CUSTOM_SHA256)
  string(APPEND COUNTLY_CONFIG_DEPENDENCIES "\nfind_dependency(OpenSSL)")
endif()
if(NOT COUNTLY_USE_CUSTOM_HTTP AND NOT WIN32)
  string(APPEND COUNTLY_CONFIG_DEPENDENCIES "\nfind_dependency(CURL)")
endif()
if(COUNTLY_USE_SQLITE AND COUNTLY_USE_SYSTEM_SQLITE)
  string(APPEND COUNTLY_CONFIG_DEPENDENCIES "\nfind_dependency(SQLite3)")
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/countlyConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/countlyConfig.cmake
  INSTALL_DESTINATION lib/cmake/countly)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/countlyConfigVersion.cmake COMPATIBILITY AnyNewerVersion)
install(EXPORT countlyTargets NAMESPACE countly:: DESTINATION lib/cmake/countly)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/countlyConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/countlyConfigVersion.cmake
  DESTINATION lib/cmake/countly)
