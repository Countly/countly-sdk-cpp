#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
#include <vector>
using namespace cly;
using namespace std;

void validateRequestsIds(std::vector<std::shared_ptr<DataEntry>> &requests, long long id) {
  for (int i = 0; i < requests.size(); ++i) {
    CHECK((requests[i]->getId()) == i + id);
  }
}

/**
 * Validate method 'RQInsertAtEnd'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void addRequestTest(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  // Try to insert an empty request
  storageModule->RQInsertAtEnd("");
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 4);

  delete storageModule;
}

/**
 * Validate method 'RQPeekFront'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateRQPeekFront(StorageModuleBase *storageModule) {
  // Try to get the front request while the queue is empty.
  CHECK(storageModule->RQPeekFront()->getId() == -1);
  CHECK(storageModule->RQPeekFront()->getData() == "");
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  CHECK(storageModule->RQPeekFront()->getData() == "request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 2");

  CHECK(storageModule->RQPeekFront()->getData() == "request");
  CHECK(storageModule->RQCount() == 2);
  delete storageModule;
}

void validateRQRemoveFront(StorageModuleBase *storageModule) {

   //Try to remove the front request from an empty queue.
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQRemoveFront();

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  // Try to remove front request by providing a wrong request
  std::shared_ptr<DataEntry> request = nullptr;
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

std::vector<std::shared_ptr<DataEntry>>requests = storageModule->RQPeekAll();
  CHECK(requests[0]->getData() == "request 1");
  CHECK(requests[1]->getData() == "request 2");
  CHECK(requests[2]->getData() == "request 3");
  CHECK(requests[0]->getId() > 0);
  validateRequestsIds(requests, 2);

  // Try to remove the request from the queue which isn't on the front.
  request.reset(new DataEntry(0, "test"));
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 3);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  request = storageModule->RQPeekFront();
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 2);

  CHECK(storageModule->RQPeekFront()->getData() == "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void RQPeakAll(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(requests.size() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 1);
  CHECK(requests.size() == 1);
  CHECK(requests.at(0)->getData() == "request");

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 4);

  requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 4);
  CHECK(requests.size() == 4);
  CHECK(requests.at(0)->getData() == "request");
  CHECK(requests.at(1)->getData() == "request 1");
  CHECK(requests.at(2)->getData() == "request 2");
  CHECK(requests.at(3)->getData() == "request 3");

    validateRequestsIds(requests, 1);
}

/**
 * Validate method 'RQClearAll'.
 *
 * @param *storageModule: a pointer to storage module.
 */
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

  SUBCASE("Validate storage method RQPeakAll") { RQPeakAll(storageModule); }
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
