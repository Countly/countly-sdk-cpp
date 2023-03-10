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
 * Validate request queue threshold.
 * Result: All the requests from the queue will remove and size of the 'RQPeekAll' list will be 0.
 * @param *storageModule: a pointer to the storage module.
 * @param *requestModule: a pointer to the request module.
 */
void ValidateRequestSizeOnReachingThresholdLimit(std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule) {

  CHECK(storageModule->RQCount() == 0);

  std::map<std::string, std::string> data = {{"param1", "value1"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 1);

  data = {{"param2", "value2"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 2);

  data = {{"param3", "value3"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 3);

  data = {{"param4", "value4"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 3);

  std::shared_ptr<DataEntry> frontRequest = storageModule->RQPeekFront();
  CHECK(frontRequest->getId() == 2);
  CHECK(frontRequest->getData().substr(0, 33) == "app_key=&device_id=&param2=value2");
}

TEST_CASE("Test Request Module with Memory Storage") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
#ifdef COUNTLY_USE_SQLITE
  configuration->databasePath = TEST_DATABASE_NAME;
#endif
  configuration->requestQueueThreshold = 3;

  std::shared_ptr<StorageModuleMemory> storageModule = std::make_shared<StorageModuleMemory>(configuration, logger);
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);
  std::shared_ptr<RequestModule> requestModule = std::make_shared<RequestModule>(configuration, logger, requestBuilder, storageModule);

  storageModule->init();

  SUBCASE("Validate request queue threshold") { ValidateRequestSizeOnReachingThresholdLimit(storageModule, requestModule); }
}

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Request Module with SQLite Storage") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = TEST_DATABASE_NAME;
  configuration->requestQueueThreshold = 3;

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);
  std::shared_ptr<RequestModule> requestModule = std::make_shared<RequestModule>(configuration, logger, requestBuilder, storageModule);

  storageModule->init();

  SUBCASE("Validate request queue threshold") { ValidateRequestSizeOnReachingThresholdLimit(storageModule, requestModule); }
}
#endif