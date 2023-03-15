#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
#include <vector>
using namespace cly;
using namespace std;

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Sqlite Storage Module Specific tests") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger(new cly::LoggerModule());
  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path, empty space") {
    configuration->databasePath = " ";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "/";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "Q:/blah";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }
}
#endif