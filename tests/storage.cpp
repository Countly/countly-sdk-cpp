#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
using namespace cly;
using namespace std;

void addRequestTest(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 4);

  delete storageModule;
}

void validateRQPeekFront(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  CHECK(storageModule->RQPeekFront() == "request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 2");

  CHECK(storageModule->RQPeekFront() == "request");
  CHECK(storageModule->RQCount() == 2);
  delete storageModule;
}

void validateRQRemoveFront(StorageModuleBase *storageModule) {

  // Try to remove the front request from an empty queue.
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQRemoveFront();

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  // Try to remove front request by providing wrong
  std::string request = "front";
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  // Removing the request from the queue which isn't on the front.
  std::string request = "request 2";
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 3);

  CHECK(storageModule->RQPeekFront() == "request 1");

  request = "request 1";
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 2);

  CHECK(storageModule->RQPeekFront() == "request 2");

  delete storageModule;
}

void RQClearAll(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQInsertAtEnd("request");

  CHECK(storageModule->RQCount() == 1);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

TEST_CASE("Test Memory Storage Module") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageModuleBase *storageModule = new StorageModuleMemory(configuration, logger);

  SUBCASE("Validate storage method RQInsertAtEnd") { addRequestTest(storageModule); }

  SUBCASE("Validate storage method RQPeekFront") { validateRQPeekFront(storageModule); }

  SUBCASE("Validate storage method RQRemoveFront") { validateRQRemoveFront(storageModule); }

  SUBCASE("Validate storage method RQClearAll") { RQClearAll(storageModule); }
}

// TEST_CASE("Storage module tests Sqlite") {
//   test_utils::clearSDK();
//   shared_ptr<cly::LoggerModule> logger;
//   logger.reset(new cly::LoggerModule());

//   shared_ptr<cly::CountlyConfiguration> configuration;
//   configuration.reset(new cly::CountlyConfiguration("", ""));
//   StorageModuleBase *storageModule = new StorageModuleDB(configuration, logger);

//   SUBCASE("validate RQInsertAtEnd method") {
//     addRequestTest(storageModule);
//   }

//    SUBCASE("validate RQPeekFront method") {
//    validateRQPeekFront(storageModule);
//   }

//   SUBCASE("validate RQRemoveFront method") {
//      validateRQRemoveFront(storageModule);

//   }

//   SUBCASE("validate RQClearAll method") {
//     RQClearAll(storageModule);
//   }
// }
