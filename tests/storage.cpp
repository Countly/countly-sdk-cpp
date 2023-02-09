#include "countly/storage_module.hpp"
#include "doctest.h"
#include <iostream>
#include <string>
using namespace cly;


TEST_CASE("Storage module tests memory") {
  //clearSDK();
  //StorageModule smm(null, null);
  SUBCASE("with a tyestt") {
    //FirstTest(smm);
  }
}


#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Storage module tests SQLite") {
  //clearSDK();
  //StorageModule smdb(null, null);
  SUBCASE("with a tyestt") {
    //FirstTest(smdb);
  }
}
#endif


void FirstTest(StorageModule sm) {
  
}