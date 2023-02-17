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
 * Validate method 'RQInsertAtEnd' with invalid request..
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateRQInsertAtEndWithInvalidRequest(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  // Try to insert an empty request
  storageModule->RQInsertAtEnd("");
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQInsertAtEnd' with valid request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateRQInsertAtEndWithInvalidRequest(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 4);

  delete storageModule;
}

/**
 * Validate method 'RQPeekFront' with empty queue.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateRQPeekFrontWithEmpthQueue(StorageModuleBase *storageModule) {
  // Try to get the front request while the queue is empty.
  CHECK(storageModule->RQPeekFront()->getId() == -1);
  CHECK(storageModule->RQPeekFront()->getData() == "");
  CHECK(storageModule->RQCount() == 0);
  delete storageModule;
}

/**
 * Validate method 'RQPeekFront' after inserting multiple requests.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateRQPeekFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  CHECK(storageModule->RQPeekFront()->getData() == "request");
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 2");

  CHECK(storageModule->RQPeekFront()->getData() == "request");
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQCount() == 2);
  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' when request queue is empty.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQRemoveFront();
  delete storageModule;
}
/**
 * Validate method 'RQRemoveFront' by providing a wrong request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithInvalidRequest(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  // Try to remove front request by providing a wrong request
  std::shared_ptr<DataEntry> request = nullptr;
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQPeekFront()->getData() == "request");
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' when request queue is empty.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQRemoveFront();
  storageModule->RQRemoveFront(nullptr);
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request with different id than front request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithRequestNotOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  // different id with different data
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(0, "request 2");
  storageModule->RQRemoveFront(request);

  CHECK(storageModule->RQCount() == 3);

  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  // different id with same data
  request = make_shared<DataEntry>(0, "request 1");
  storageModule->RQRemoveFront(request);

  CHECK(storageModule->RQCount() == 3);

  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request with same id as front request id.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithRequestNotOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  // same id with different data
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(1, "");
  storageModule->RQRemoveFront(request);

  CHECK(storageModule->RQCount() == 3);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  // same id with same data
  request = make_shared<DataEntry>(1, "request 1");
  storageModule->RQRemoveFront(request);

  CHECK(storageModule->RQCount() == 3);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithRequestOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  // same id with different data
  std::shared_ptr<DataEntry> request = storageModule->RQPeekFront();
  CHECK(storageModule->RQPeekFront().get() == request.get());

  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 2);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 2);
  CHECK(storageModule->RQPeekFront()->getData() == "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' with invalid request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateInvalidRequestRemoval(StorageModuleBase *storageModule) {
  // Try to remove front request by providing a wrong request
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(-1, "");
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 1);

  request = storageModule->RQPeekFront();
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQInsertAtEnd("request");
  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 1);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by removing same request twice.
 *
 * @param *storageModule: a pointer to storage module.
 */
void validateSameRequestRemove(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 2);
  CHECK(storageModule->RQPeekAll().size() == 2);

  std::shared_ptr<DataEntry> front = storageModule->RQPeekFront();
  storageModule->RQRemoveFront(front);
  storageModule->RQRemoveFront(front);
  CHECK(storageModule->RQCount() == 1);
  CHECK(storageModule->RQPeekAll().size() == 1);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' without providing a request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQRemoveFront_WithoutRequest(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  std::shared_ptr<DataEntry> request = storageModule->RQPeekFront();
  CHECK(storageModule->RQPeekFront().get() == request.get());

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 2);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 2);
  CHECK(storageModule->RQPeekFront()->getData() == "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' with empty queue.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);

  std::vector<std::shared_ptr<DataEntry>> requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(requests.size() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after removing front request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_WithRemovingFrontRequest(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 1);
  CHECK(requests.size() == 1);

  CHECK(requests.at(0).get() == storageModule->RQPeekFront().get());
  CHECK(requests.at(0)->getId() == 1);
  CHECK(requests.at(0)->getData() == "request");

  storageModule->RQRemoveFront();
  CHECK(requests.size() == 1);

  requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(requests.size() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' with queue front request.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  storageModule->RQInsertAtEnd("request 1");
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 1);

  CHECK(storageModule->RQPeekFront().get() != requests[0].get());
  CHECK(storageModule->RQPeekFront()->getId() == requests[0]->getId());
  CHECK(storageModule->RQPeekFront()->getData() == requests[0]->getData());

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after calling 'RQClearAll'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_WithNonEmptyQueue(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
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

  storageModule->RQClearAll();
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on empty queue.
 *
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  CHECK(storageModule->RQCount() == 0);
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);

  storageModule->RQClearAll();
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void RQPeakAll(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;

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

  // validating 'RQPeekAll' method after inserting
  requests = storageModule->RQPeekAll(); // peaeking all requests
  CHECK(requests.size() == 4);
  CHECK(storageModule->RQCount() == 4);
  CHECK(requests.at(0)->getData() == "request");
  CHECK(requests.at(1)->getData() == "request 1");
  CHECK(requests.at(2)->getData() == "request 2");
  CHECK(requests.at(3)->getData() == "request 3");

  validateRequestsIds(requests, 1);
}

/**
 * Validate method 'RQClearAll' when request is empty.
 *
 * @param *storageModule: a pointer to storage module.
 */
void RQClearAll(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQClearAll'.
 *
 * @param *storageModule: a pointer to storage module.
 */
void RQClearAll(StorageModuleBase *storageModule) {

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);
  CHECK(storageModule->RQPeekAll() == 1);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(storageModule->RQPeekAll() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);
  CHECK(storageModule->RQPeekAll() == 3);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(storageModule->RQPeekAll() == 0);

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

  SUBCASE("Validate removing same request multiple times") { validateSameRequestRemove(storageModule); }

  SUBCASE("Validate removing request with worong id and already removed request") { validateInvalidRequestRemoval(storageModule); }
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
