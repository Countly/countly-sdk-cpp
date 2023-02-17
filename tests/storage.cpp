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

/**
 * Validates ids of all requests in vector.
 * 
 */
void validateRequestsIds(std::vector<std::shared_ptr<DataEntry>> &requests, long long id) {
  for (int i = 0; i < requests.size(); ++i) {
    CHECK((requests[i]->getId()) == i + id);
  }
}

/**
 * Validate method 'RQInsertAtEnd' with invalid request.
 * Result: Request will not add into the request queue.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQInsertAtEndWithInvalidRequest(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  // Try to insert an empty request
  storageModule->RQInsertAtEnd("");
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQInsertAtEnd' with valid request.
 * Result: Request will add into the request queue.
 * @param *storageModule: a pointer to storage module.
 */
void testRQInsertAtEndWithRequest(StorageModuleBase *storageModule) {
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
 * Result: RQPeekFront will return request having id '-1' and data an empty string.
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeekFrontWithEmpthQueue(StorageModuleBase *storageModule) {
  // Try to get the front request while the queue is empty.
  CHECK(storageModule->RQPeekFront()->getId() == -1);
  CHECK(storageModule->RQPeekFront()->getData() == "");
  CHECK(storageModule->RQCount() == 0);
  delete storageModule;
}

/**
 * Validate method 'RQPeekFront' after inserting multiple requests.
 * Result: All requests will add to the request queue and the first request will always be in front of the queue. 
 * @param *storageModule: a pointer to the storage module.
 */
void testRQPeekFront(StorageModuleBase *storageModule) {
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
 * Validate method 'RQRemoveFront' when the request queue is empty.
 * Result: Code will not break and queue size will be '0'
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFront_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQRemoveFront();
  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a wrong request.
 * Result: The request will not remove from the queue and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
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
 * Validate method 'RQRemoveFront(request)' when the request queue is empty.
 * Result: Code will not break and queue size will be '0'
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFront2_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);

  storageModule->RQRemoveFront();
  storageModule->RQRemoveFront(nullptr);
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request with a different id than the front request.
 * Result: The request will not remove from the queue, the front request and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
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
 * Validate method 'RQRemoveFront' by providing a request with the same id as the front request id.
 * Result: The request will not remove from the queue, the front request and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFrontWithSameId_WithRequestNotOnFront(StorageModuleBase *storageModule) {
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
 * Validate method 'RQRemoveFront' by providing a valid request.
 * Result: The request will remove from the front of the queue.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFront_WithRequestOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  std::shared_ptr<DataEntry> request = storageModule->RQPeekFront();
  CHECK(storageModule->RQPeekFront().get() == request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 1);
  CHECK(storageModule->RQPeekFront()->getData() == "request 1");

  storageModule->RQRemoveFront(request);
  CHECK(storageModule->RQCount() == 2);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  CHECK(storageModule->RQPeekFront()->getId() == 2);
  CHECK(storageModule->RQPeekFront()->getData() == "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' with an invalid request when the request queue is empty.
 * Result: Code will not break, the size of request queue will remain '0'
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFrontOnEmptyQueue_WithInvalidRequest(StorageModuleBase *storageModule) {
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
 * Validate method 'RQRemoveFront' by removing the same request twice.
 * Result: The request will remove from the queue only first time.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemove_WithSameRequestTwice(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 2);
  CHECK(storageModule->RQPeekAll().size() == 2);

  std::shared_ptr<DataEntry> front = storageModule->RQPeekFront();
  storageModule->RQRemoveFront(front);
  CHECK(storageModule->RQCount() == 1);
  CHECK(storageModule->RQPeekAll().size() == 1);

  storageModule->RQRemoveFront(front);
  CHECK(storageModule->RQCount() == 1);
  CHECK(storageModule->RQPeekAll().size() == 1);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront()'
 * Result: Request will remove from the front of the queue.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQRemoveFront2(StorageModuleBase *storageModule) {
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
 * Validate method 'RQPeekAll' with an empty queue.
 * Result: The returned list of the queue will be empty.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQPeakAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);

  std::vector<std::shared_ptr<DataEntry>> requests = storageModule->RQPeekAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(requests.size() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after removing the front request.
 * Result: The size of the list returned by 'RQPeekAll' will decrease.
 * @param *storageModule: a pointer to the storage module.
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
 * Result: The front request and the first element of the list will be the same.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQPeakAll_WithFrontRequest(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  storageModule->RQInsertAtEnd("request 1");
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 1);

  CHECK(storageModule->RQPeekFront().get() == requests[0].get());
  CHECK(storageModule->RQPeekFront()->getId() == requests[0]->getId());
  CHECK(storageModule->RQPeekFront()->getData() == requests[0]->getData());

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after calling 'RQClearAll'.
 * Result: 'RQPeekAll' will return an empty list.
 * @param *storageModule: a pointer to storage module.
 */
void testRQPeakAll_OnNonEmptyQueueAfterClearAll(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  storageModule->RQInsertAtEnd("request");
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
 * Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on an empty queue.
 * Result: the list returned 'RQPeekAll' will be empty.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQPeakAll_WithEmptyQueueAndClearAll(StorageModuleBase *storageModule) {
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
 * Validate method 'RQPeekAll' while inserting multiple valid requests.
 * Result: Reqeust will add to the queue and the size of the list returned will increase on every insert. 
 * @param *storageModule: a pointer to the storage module.
 */
void testRQPeakAll_WithMultipleRequests(StorageModuleBase *storageModule) {
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
 * Validate method 'RQClearAll' when the request queue is empty.
 * Result: Code will not break and the size of the queue will remain 0.
 * @param *storageModule: a pointer to the storage module.
 */
void testRQClearAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQClearAll' when the request queue is not empty.
 * Result: All the requests from the queue will remove and size of the 'RQPeekAll' list will be 0.
 * @param *storageModule: a pointer to storage module.
 */
void RQClearAll_WithNonEmptyQueue(StorageModuleBase *storageModule) {

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);
  CHECK(storageModule->RQPeekAll().size() == 1);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);
  CHECK(storageModule->RQPeekAll().size() == 3);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == 0);
  CHECK(storageModule->RQPeekAll().size() == 0);

  delete storageModule;
}

TEST_CASE("Test Storage Module: Validate 'RQInsertAtEnd' method") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageModuleBase *storageModule = new StorageModuleMemory(configuration, logger);

  SUBCASE("Validate method 'RQInsertAtEnd' with invalid request") { testRQInsertAtEndWithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQInsertAtEnd' with Valid requests") { testRQInsertAtEndWithRequest(storageModule); }

  SUBCASE("Validate method 'RQPeekFront' with empty queue") { testRQPeekFrontWithEmpthQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekFront' after inserting multiple requests.") { testRQPeekFront(storageModule); }

  SUBCASE("Validate method 'RQRemoveFront' when the request queue is empty.") { testRQRemoveFront_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a wrong request") { testRQRemoveFront_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { testRQRemoveFront_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { testRQRemoveFrontWithSameId_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront(request)' when the request queue is empty.") { testRQRemoveFront2_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a valid request.") { testRQRemoveFront_WithRequestOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' with an invalid request when the request queue is empty.") { testRQRemoveFrontOnEmptyQueue_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by removing the same request twice.") { testRQRemove_WithSameRequestTwice(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront().") { testRQRemoveFront2(storageModule); }

  SUBCASE("Validate method 'RQPeekAll' with an empty queue.") { testRQPeakAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after removing the front request.") { testRQPeakAll_WithRemovingFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' with queue front request") { testRQPeakAll_WithFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after calling 'RQClearAll'.") { testRQPeakAll_OnNonEmptyQueueAfterClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on an empty queue.") { testRQPeakAll_WithEmptyQueueAndClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' while inserting multiple requests.") { testRQPeakAll_WithMultipleRequests(storageModule); }

  SUBCASE("Validate method 'RQClearAll' when the request queue is empty.") { testRQClearAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQClearAll' when the request queue is not empty.") { RQClearAll_WithNonEmptyQueue(storageModule); }
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
