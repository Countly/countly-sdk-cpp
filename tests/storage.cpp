#include "countly/storage_base.hpp"
#include "countly/storage_module.hpp"
#include "countly/sqlite_storage_module.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
using namespace cly;
using namespace std;

void addEventTest(StorageBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);
}


TEST_CASE("Storage module tests memory") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageBase *storageModule = new StorageModule(configuration, logger);
  
  SUBCASE("add request") {
    storageModule->RQInsertAtEnd("request");
    CHECK(storageModule->RQCount() == 1);
  }

   SUBCASE("validate RQPeekFront request") {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");
   CHECK(storageModule->RQCount() == 1);

   CHECK(storageModule->RQPeekFront() == "request");
   CHECK(storageModule->RQCount() == 1);
  }

  SUBCASE("validate RQRemoveFront request") {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");

   CHECK(storageModule->RQCount() == 1);
   storageModule->RQRemoveFront();
   CHECK(storageModule->RQCount() == 0);
  }

  SUBCASE("validate RQClearAll request") {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");
   
   CHECK(storageModule->RQCount() == 1);
   storageModule->RQClearAll();
   CHECK(storageModule->RQCount() == 0);
  }
}


