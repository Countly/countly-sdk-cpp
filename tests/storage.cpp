#include "countly/storage_base.hpp"
#include "countly/storage_module.hpp"
#include "countly/sqlite_storage_module.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
using namespace cly;
using namespace std;

void addRequestTest(StorageBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);
  delete storageModule;
}

void validateRQPeekFront(StorageBase *storageModule) {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");
   CHECK(storageModule->RQCount() == 1);

   CHECK(storageModule->RQPeekFront() == "request");
   CHECK(storageModule->RQCount() == 1);
   delete storageModule;
}

void validateRQRemoveFront(StorageBase *storageModule) {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");

   CHECK(storageModule->RQCount() == 1);
   storageModule->RQRemoveFront();
   CHECK(storageModule->RQCount() == 0);  
   delete storageModule;

}

void RQClearAll(StorageBase *storageModule) {
   CHECK(storageModule->RQCount() == 0);
   storageModule->RQInsertAtEnd("request");
   
   CHECK(storageModule->RQCount() == 1);
   storageModule->RQClearAll();
   CHECK(storageModule->RQCount() == 0);  
   delete storageModule;

}


TEST_CASE("Storage module tests memory") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageBase *storageModule = new StorageModule(configuration, logger);

  SUBCASE("validate RQInsertAtEnd method") {
    addRequestTest(storageModule);
  }

   SUBCASE("validate RQPeekFront method") {
   validateRQPeekFront(storageModule);
  }

  SUBCASE("validate RQRemoveFront method") {
     validateRQRemoveFront(storageModule);

  }

  SUBCASE("validate RQClearAll method") {
    RQClearAll(storageModule);
  }
}

TEST_CASE("Storage module tests Sqlite") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageBase *storageModule = new SqliteStorageModule(configuration, logger);

  SUBCASE("validate RQInsertAtEnd method") {
    addRequestTest(storageModule);
  }

   SUBCASE("validate RQPeekFront method") {
   validateRQPeekFront(storageModule);
  }

  SUBCASE("validate RQRemoveFront method") {
     validateRQRemoveFront(storageModule);

  }

  SUBCASE("validate RQClearAll method") {
    RQClearAll(storageModule);
  }
}


