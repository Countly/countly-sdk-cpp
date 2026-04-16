#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include "countly/request_builder.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <ctime>
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
  std::string requestData = frontRequest->getData();
  CHECK(requestData.find("app_key=") != std::string::npos);
  CHECK(requestData.find("device_id=") != std::string::npos);
  CHECK(requestData.find("param2=value2") != std::string::npos);
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
  std::shared_ptr<ConfigurationModule> configurationModule = std::make_shared<ConfigurationModule>(nullptr, configuration, logger, requestBuilder, storageModule, requestModule, std::make_shared<std::mutex>());

  requestModule->setConfigurationProvider(configurationModule);
  storageModule->init();
  configurationModule->fetchConfigFromStorage();

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
  std::shared_ptr<ConfigurationModule> configurationModule = std::make_shared<ConfigurationModule>(nullptr, configuration, logger, requestBuilder, storageModule, requestModule, std::make_shared<std::mutex>());

  requestModule->setConfigurationProvider(configurationModule);
  storageModule->init();
  configurationModule->fetchConfigFromStorage();

  SUBCASE("Validate request queue threshold") { ValidateRequestSizeOnReachingThresholdLimit(storageModule, requestModule); }
}
#endif

// Helper to parse a serialized request string into a key-value map
static std::map<std::string, std::string> parseRequest(const std::string &data) {
  std::map<std::string, std::string> result;
  std::string::size_type start = 0;
  while (start < data.size()) {
    auto ampersand = data.find('&', start);
    if (ampersand == std::string::npos) {
      ampersand = data.size();
    }
    auto eq = data.find('=', start);
    if (eq != std::string::npos && eq < ampersand) {
      result[data.substr(start, eq - start)] = data.substr(eq + 1, ampersand - eq - 1);
    }
    start = ampersand + 1;
  }
  return result;
}

TEST_CASE("Requests contain dow, hour, and tz") {
  shared_ptr<cly::LoggerModule> logger = std::make_shared<cly::LoggerModule>();
  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("test_app_key", "test_device_id");
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);

  SUBCASE("buildRequest includes dow, hour, and tz fields") {
    std::map<std::string, std::string> data = {{"test_key", "test_value"}};
    std::string request = requestBuilder->buildRequest(data);

    auto parsed = parseRequest(request);

    CHECK(parsed.find("dow") != parsed.end());
    CHECK(parsed.find("hour") != parsed.end());
    CHECK(parsed.find("tz") != parsed.end());
    CHECK(parsed.find("timestamp") != parsed.end());
  }

  SUBCASE("dow is between 0 and 6") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int dow = std::stoi(parsed["dow"]);
    CHECK(dow >= 0);
    CHECK(dow <= 6);
  }

  SUBCASE("hour is between 0 and 23") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int hour = std::stoi(parsed["hour"]);
    CHECK(hour >= 0);
    CHECK(hour <= 23);
  }

  SUBCASE("tz is a valid timezone offset in minutes") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int tz = std::stoi(parsed["tz"]);
    // Valid timezone offsets range from UTC-12 (-720) to UTC+14 (+840)
    CHECK(tz >= -720);
    CHECK(tz <= 840);
  }

  SUBCASE("dow and hour match current local time") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);

    CHECK(std::stoi(parsed["dow"]) == local_tm.tm_wday);
    CHECK(std::stoi(parsed["hour"]) == local_tm.tm_hour);
  }

  SUBCASE("tz matches current timezone offset") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);
    std::tm gm_tm = *std::gmtime(&now);

    int expected_tz = (local_tm.tm_hour - gm_tm.tm_hour) * 60 + (local_tm.tm_min - gm_tm.tm_min);
    int day_diff = local_tm.tm_mday - gm_tm.tm_mday;
    if (day_diff > 1) {
      day_diff = -1;
    } else if (day_diff < -1) {
      day_diff = 1;
    }
    expected_tz += day_diff * 24 * 60;

    CHECK(std::stoi(parsed["tz"]) == expected_tz);
  }
}