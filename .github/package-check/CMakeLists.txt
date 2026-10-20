cmake_minimum_required(VERSION 3.13...3.31)
project(package_check CXX)

# Uses the SDK only through find_package, the way an app that installed it or got it from vcpkg or Conan does
find_package(countly CONFIG REQUIRED)
add_executable(app app.cpp)
target_link_libraries(app PRIVATE countly::countly)
if(EXPECT_COUNTLY_SQLITE)
  target_compile_definitions(app PRIVATE EXPECT_COUNTLY_SQLITE)
endif()
