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
 * Validates request.
 */
void validateSizes(StorageModuleBase *storageModule, long long size) {
  CHECK(storageModule->RQCount() == size);
  CHECK(storageModule->RQPeekAll().size() == size);
}

/**
 * Validates request.
 */
void validateDataEntry(std::shared_ptr<DataEntry> testedEntry, long long id, std::string data) {
  CHECK(testedEntry->getId() == id);
  CHECK(testedEntry->getData() == data);
}

/**
 * Validates ids of all requests in vector.
 */
void validateRequestIds(std::vector<std::shared_ptr<DataEntry>> &requests, long long id) {
  for (int i = 0; i < requests.size(); ++i) {
    CHECK((requests[i]->getId()) == i + id);
  }
}

/**
 * Validate method 'RQInsertAtEnd' with invalid request.
 * Result: Request will not add into the request queue.
 * @param *storageModule: a pointer to the storage module.
 */
void RQInsertAtEndWithInvalidRequest(StorageModuleBase *storageModule) {
  CHECK(storageModule->RQCount() == 0);
  // Try to insert an empty request
  storageModule->RQInsertAtEnd("");
  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQInsertAtEnd' with valid request.
 * Result: Request will add into the request queue.
 * @param *storageModule: a pointer to storage module.
 */
void RQInsertAtEndWithRequest(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  validateSizes(storageModule, 4);

  delete storageModule;
}

/**
 * Validate method 'RQPeekFront' with empty queue.
 * Result: RQPeekFront will return request having id '-1' and data an empty string.
 * @param *storageModule: a pointer to storage module.
 */
void RQPeekFrontWithEmpthQueue(StorageModuleBase *storageModule) {
  // Try to get the front request while the queue is empty.
  validateDataEntry(storageModule->RQPeekFront(), -1, "");

  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekFront' after inserting multiple requests.
 * Result: All requests will add to the request queue and the first request will always be in front of the queue.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeekFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  validateDataEntry(storageModule->RQPeekFront(), 1, "request");

  CHECK(storageModule->RQCount() == 1);

  storageModule->RQInsertAtEnd("request 2");

  validateDataEntry(storageModule->RQPeekFront(), 1, "request");

  validateSizes(storageModule, 2);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' when the request queue is empty.
 * Result: Code will not break and queue size will be '0'
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFront_WithEmptyQueue(StorageModuleBase *storageModule) {
  validateSizes(storageModule, 0);

  storageModule->RQRemoveFront();
  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a wrong request.
 * Result: The request will not remove from the queue and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFront_WithInvalidRequest(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  // Try to remove front request by providing a wrong request
  std::shared_ptr<DataEntry> request = nullptr;
  storageModule->RQRemoveFront(request);

  validateDataEntry(storageModule->RQPeekFront(), 1, "request");
  validateSizes(storageModule, 1);

  storageModule->RQRemoveFront();
  CHECK(storageModule->RQCount() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront(request)' when the request queue is empty.
 * Result: Code will not break and queue size will be '0'
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFront2_WithEmptyQueue(StorageModuleBase *storageModule) {
  validateSizes(storageModule, 0);

  storageModule->RQRemoveFront();
  storageModule->RQRemoveFront(nullptr);
  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request with a different id than the front request.
 * Result: The request will not remove from the queue, the front request and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFront_WithRequestNotOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  validateSizes(storageModule, 3);

  // different id with different data
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(1, "");
  storageModule->RQRemoveFront(request);

  validateSizes(storageModule, 2);

  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  // different id with same data
  request = make_shared<DataEntry>(0, "request 1");
  storageModule->RQRemoveFront(request);

  validateSizes(storageModule, 2);

  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a request with the same id as the front request id.
 * Result: The request will remove from the queue, the front request and the size of the queue will remain the same.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFrontWithSameId_WithRequestNotOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  validateSizes(storageModule, 3);

  // same id with different data
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(1, "");
  storageModule->RQRemoveFront(request);

  validateSizes(storageModule, 2);
  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  // same id with same data
  request = make_shared<DataEntry>(1, "request 1");
  storageModule->RQRemoveFront(request);

  validateSizes(storageModule, 2);
  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by providing a valid request.
 * Result: The request will remove from the front of the queue.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFront_WithRequestOnFront(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  validateSizes(storageModule, 2);

  std::shared_ptr<DataEntry> request = storageModule->RQPeekFront();
  CHECK(storageModule->RQPeekFront().get() == request.get());
  validateDataEntry(storageModule->RQPeekFront(), 1, "request 1");

  storageModule->RQRemoveFront(request);
  validateSizes(storageModule, 1);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' with an invalid request when the request queue is empty.
 * Result: Code will not break, the size of request queue will remain '0'
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFrontOnEmptyQueue_WithInvalidRequest(StorageModuleBase *storageModule) {
  // Try to remove front request by providing a wrong request
  std::shared_ptr<DataEntry> request = make_shared<DataEntry>(-1, "");
  storageModule->RQRemoveFront(request);
  validateSizes(storageModule, 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 1, "request");

  validateSizes(storageModule, 1);
  storageModule->RQRemoveFront(request);
  validateSizes(storageModule, 1);

  request = storageModule->RQPeekFront();
  validateSizes(storageModule, 1);

  storageModule->RQRemoveFront();
  validateSizes(storageModule, 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 2, "request");

  storageModule->RQRemoveFront(request);
  validateSizes(storageModule, 1);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront' by removing the same request twice.
 * Result: The request will remove from the queue only first time.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemove_WithSameRequestTwice(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request");
  storageModule->RQInsertAtEnd("request");
  validateSizes(storageModule, 2);

  std::shared_ptr<DataEntry> front = storageModule->RQPeekFront();
  validateDataEntry(storageModule->RQPeekFront(), 1, "request");

  storageModule->RQRemoveFront(front);

  validateSizes(storageModule, 1);

  storageModule->RQRemoveFront(front);

  validateSizes(storageModule, 1);

  delete storageModule;
}

/**
 * Validate method 'RQRemoveFront()'
 * Result: Request will remove from the front of the queue.
 * @param *storageModule: a pointer to the storage module.
 */
void RQRemoveFrontWithoutRequestParam(StorageModuleBase *storageModule) {
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  validateSizes(storageModule, 3);

  std::shared_ptr<DataEntry> request = storageModule->RQPeekFront();
  validateDataEntry(storageModule->RQPeekFront(), 1, "request 1");

  storageModule->RQRemoveFront();
  validateSizes(storageModule, 2);

  CHECK(storageModule->RQPeekFront().get() != request.get());
  validateDataEntry(storageModule->RQPeekFront(), 2, "request 2");

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' with an empty queue.
 * Result: The returned list of the queue will be empty.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeakAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  validateSizes(storageModule, 0);

  std::vector<std::shared_ptr<DataEntry>> requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 0);
  CHECK(requests.size() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after removing the front request.
 * Result: The size of the list returned by 'RQPeekAll' will decrease.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeakAll_WithRemovingFrontRequest(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;

  storageModule->RQInsertAtEnd("request");
  validateSizes(storageModule, 1);

  requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 1);
  CHECK(requests.size() == 1);

  CHECK(requests.at(0).get() == storageModule->RQPeekFront().get());
  validateDataEntry(storageModule->RQPeekFront(), 1, "request");

  storageModule->RQRemoveFront();
  CHECK(requests.size() == 1);

  requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 0);
  CHECK(requests.size() == 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' with queue front request.
 * Result: The front request and the first element of the list will be the same.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeakAll_WithFrontRequest(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  storageModule->RQInsertAtEnd("request 1");
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 1);

  validateDataEntry(storageModule->RQPeekFront(), 1, "request 1");

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after calling 'RQClearAll'.
 * Result: 'RQPeekAll' will return an empty list.
 * @param *storageModule: a pointer to storage module.
 */
void RQPeakAll_OnNonEmptyQueueAfterClearAll(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  storageModule->RQInsertAtEnd("request");
  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");

  validateSizes(storageModule, 4);

  requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 4);
  CHECK(requests.size() == 4);

  validateDataEntry(requests.at(0), 1, "request");
  validateDataEntry(requests.at(1), 2, "request 1");
  validateDataEntry(requests.at(2), 3, "request 2");
  validateDataEntry(requests.at(3), 4, "request 3");

  validateRequestIds(requests, 1);

  storageModule->RQClearAll();
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);
  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on an empty queue.
 * Result: the list returned 'RQPeekAll' will be empty.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeakAll_WithEmptyQueueAndClearAll(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;
  validateSizes(storageModule, 0);
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);

  storageModule->RQClearAll();
  requests = storageModule->RQPeekAll();
  CHECK(requests.size() == 0);
  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQPeekAll' while inserting multiple valid requests.
 * Result: Reqeust will add to the queue and the size of the list returned will increase on every insert.
 * @param *storageModule: a pointer to the storage module.
 */
void RQPeakAll_WithMultipleRequests(StorageModuleBase *storageModule) {
  std::vector<std::shared_ptr<DataEntry>> requests;

  storageModule->RQInsertAtEnd("request");
  validateSizes(storageModule, 1);

  requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 1);
  CHECK(requests.size() == 1);

  validateDataEntry(requests.at(0), 1, "request");

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  validateSizes(storageModule, 4);

  requests = storageModule->RQPeekAll();
  validateSizes(storageModule, 4);

  validateDataEntry(requests.at(0), 1, "request");
  validateDataEntry(requests.at(1), 2, "request 1");
  validateDataEntry(requests.at(2), 3, "request 2");
  validateDataEntry(requests.at(3), 4, "request 3");

  validateRequestIds(requests, 1);

  // validating 'RQPeekAll' method after inserting
  requests = storageModule->RQPeekAll(); // peaeking all requests
  CHECK(requests.size() == 4);
  validateSizes(storageModule, 4);
  validateDataEntry(requests.at(0), 1, "request");
  validateDataEntry(requests.at(1), 2, "request 1");
  validateDataEntry(requests.at(2), 3, "request 2");
  validateDataEntry(requests.at(3), 4, "request 3");

  validateRequestIds(requests, 1);
}

/**
 * Validate method 'RQClearAll' when the request queue is empty.
 * Result: Code will not break and the size of the queue will remain 0.
 * @param *storageModule: a pointer to the storage module.
 */
void RQClearAll_WithEmptyQueue(StorageModuleBase *storageModule) {
  validateSizes(storageModule, 0);
  storageModule->RQClearAll();
  validateSizes(storageModule, 0);

  delete storageModule;
}

/**
 * Validate method 'RQClearAll' when the request queue is not empty.
 * Result: All the requests from the queue will remove and size of the 'RQPeekAll' list will be 0.
 * @param *storageModule: a pointer to the storage module.
 */
void RQClearAll_WithNonEmptyQueue(StorageModuleBase *storageModule) {

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == 1);

  storageModule->RQClearAll();
  validateSizes(storageModule, 0);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQInsertAtEnd("request 1");
  storageModule->RQInsertAtEnd("request 2");
  storageModule->RQInsertAtEnd("request 3");
  CHECK(storageModule->RQCount() == 3);

  storageModule->RQClearAll();
  validateSizes(storageModule, 0);
  CHECK(storageModule->RQPeekAll().size() == 0);

  delete storageModule;
}

TEST_CASE("Test Memory Storage Module") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));
  StorageModuleBase *storageModule = new StorageModuleMemory(configuration, logger);

  SUBCASE("Validate method 'RQInsertAtEnd' with invalid request") { RQInsertAtEndWithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQInsertAtEnd' with Valid requests") { RQInsertAtEndWithRequest(storageModule); }

  SUBCASE("Validate method 'RQPeekFront' with empty queue") { RQPeekFrontWithEmpthQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekFront' after inserting multiple requests.") { RQPeekFront(storageModule); }

  SUBCASE("Validate method 'RQRemoveFront' when the request queue is empty.") { RQRemoveFront_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a wrong request") { RQRemoveFront_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { RQRemoveFront_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { RQRemoveFrontWithSameId_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront(request)' when the request queue is empty.") { RQRemoveFront2_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a valid request.") { RQRemoveFront_WithRequestOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' with an invalid request when the request queue is empty.") { RQRemoveFrontOnEmptyQueue_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by removing the same request twice.") { RQRemove_WithSameRequestTwice(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront().") { RQRemoveFrontWithoutRequestParam(storageModule); }

  SUBCASE("Validate method 'RQPeekAll' with an empty queue.") { RQPeakAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after removing the front request.") { RQPeakAll_WithRemovingFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' with queue front request") { RQPeakAll_WithFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after calling 'RQClearAll'.") { RQPeakAll_OnNonEmptyQueueAfterClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on an empty queue.") { RQPeakAll_WithEmptyQueueAndClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' while inserting multiple requests.") { RQPeakAll_WithMultipleRequests(storageModule); }

  SUBCASE("Validate method 'RQClearAll' when the request queue is empty.") { RQClearAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQClearAll' when the request queue is not empty.") { RQClearAll_WithNonEmptyQueue(storageModule); }
}

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Memory Storage Module") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration;
  configuration.reset(new cly::CountlyConfiguration("", ""));

  StorageModuleBase *storageModule = new StorageModuleDB(configuration, logger);

  SUBCASE("Validate method 'RQInsertAtEnd' with invalid request") { RQInsertAtEndWithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQInsertAtEnd' with Valid requests") { RQInsertAtEndWithRequest(storageModule); }

  SUBCASE("Validate method 'RQPeekFront' with empty queue") { RQPeekFrontWithEmpthQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekFront' after inserting multiple requests.") { RQPeekFront(storageModule); }

  SUBCASE("Validate method 'RQRemoveFront' when the request queue is empty.") { RQRemoveFront_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a wrong request") { RQRemoveFront_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { RQRemoveFront_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a request with a different id than the front request") { RQRemoveFrontWithSameId_WithRequestNotOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront(request)' when the request queue is empty.") { RQRemoveFront2_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by providing a valid request.") { RQRemoveFront_WithRequestOnFront(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' with an invalid request when the request queue is empty.") { RQRemoveFrontOnEmptyQueue_WithInvalidRequest(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront' by removing the same request twice.") { RQRemove_WithSameRequestTwice(storageModule); }
  SUBCASE("Validate method 'RQRemoveFront().") { RQRemoveFrontWithoutRequestParam(storageModule); }

  SUBCASE("Validate method 'RQPeekAll' with an empty queue.") { RQPeakAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after removing the front request.") { RQPeakAll_WithRemovingFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' with queue front request") { RQPeakAll_WithFrontRequest(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after calling 'RQClearAll'.") { RQPeakAll_OnNonEmptyQueueAfterClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' after clearing calling 'RQClearAll' on an empty queue.") { RQPeakAll_WithEmptyQueueAndClearAll(storageModule); }
  SUBCASE("Validate method 'RQPeekAll' while inserting multiple requests.") { RQPeakAll_WithMultipleRequests(storageModule); }

  SUBCASE("Validate method 'RQClearAll' when the request queue is empty.") { RQClearAll_WithEmptyQueue(storageModule); }
  SUBCASE("Validate method 'RQClearAll' when the request queue is not empty.") { RQClearAll_WithNonEmptyQueue(storageModule); }
}
#endif