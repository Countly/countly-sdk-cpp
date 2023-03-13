#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
#include <vector>
using namespace cly;
using namespace std;

void storageModuleWithoutInit(std::shared_ptr<StorageModuleBase> storageModule) {
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  std::shared_ptr<DataEntry> entry = storageModule->RQPeekFront();
  CHECK(entry->getId() == -1);
  CHECK(entry->getData() == "");

  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);
}

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Sqlite Storage Module Specific tests") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = TEST_DATABASE_NAME;

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  storageModule->init();
}

TEST_CASE("Test SQlite Storage Module against invalid database path") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = "";

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  storageModuleWithoutInit(storageModule);
}

TEST_CASE("Test SQlite Storage Module against invalid database path") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = "/";

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  storageModuleWithoutInit(storageModule);
}

TEST_CASE("Test SQlite Storage Module against invalid database path") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = "Q:/blah";

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  storageModuleWithoutInit(storageModule);
}
#endif