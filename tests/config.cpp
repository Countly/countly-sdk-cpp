#include "countly.hpp"
#include "doctest.h"
#include "test_utils.hpp"
using namespace cly;
using namespace test_utils;

// Define custom SHA256 functions
std::string customSha_1_returnValue = "SHA256_1";
std::string customSha256_1(const std::string &data) { return customSha_1_returnValue; }
std::string customSha256_2(const std::string &data) { return "SHA256_2"; }

// Define custom HTTPClient function
HTTPResponse customClient(bool f, const std::string &a, const std::string &b) {
  HTTPResponse response;
  response.success = true;
  response.data = "data";

  return response;
}

// Define new custom HTTPClient function
HTTPResponse new_CustomClient(bool f, const std::string &a, const std::string &b) {
  HTTPResponse response;
  response.success = false;
  response.data = "new-data";

  return response;
}

// Test case to validate Countly configuration using all setters
TEST_CASE("Validate setting configuration values") {
  // Test case for default values
  // Making sure that the default configuration values are as expected
  SUBCASE("Validating default values") {
    clearSDK();
    Countly &ct = Countly::getInstance();
    const CountlyConfiguration config = ct.getConfiguration();

    // Validate default configuration values
    CHECK(config.serverUrl == "");
    CHECK(config.appKey == "");
    CHECK(config.deviceId == "");
    CHECK(config.salt == "");
#ifdef COUNTLY_USE_SQLITE
    CHECK(config.databasePath == "");
#endif
    CHECK(config.sessionDuration == 60);
    CHECK(config.eventQueueThreshold == 100);
    CHECK(config.requestQueueThreshold == 1000);
    CHECK(config.maxProcessingBatchSize == 100);
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == false);
    CHECK(config.port == 443);
    CHECK(config.sha256_function == nullptr);
    CHECK(config.http_client_function == nullptr);
    CHECK(config.metrics.empty());
  }

  // Test case to validate values set using Countly setters
  // Making sure all the values can be set
  SUBCASE("Validating configuration setters") {
    clearSDK();
    Countly &ct = Countly::getInstance();
    SHA256Function funPtr = customSha256_1;
    HTTPClientFunction clientPtr = customClient;

    // Set configuration values using Countly setters
    ct.alwaysUsePost(true);
    ct.setDeviceID("test-device-id");
    ct.setSha256(funPtr);
    ct.setHTTPClient(clientPtr);
    ct.SetMetrics("Windows 10", "10.22", "pc", "800x600", "Carrier", "1.0");
    ct.SetMaxEventsPerMessage(10);
    ct.setAutomaticSessionUpdateInterval(5);
    ct.setSalt("salt");
    ct.setMaxRequestQueueSize(10);
    ct.SetPath(TEST_DATABASE_NAME);
    ct.setMaxRQProcessingBatchSize(10);
    ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);

    // Get configuration values using Countly getters
    const CountlyConfiguration config = ct.getConfiguration();
    CHECK(config.serverUrl == "https://try.count.ly");
    CHECK(config.appKey == "YOUR_APP_KEY");
    CHECK(config.deviceId == "test-device-id");
    CHECK(config.salt == "salt");
#ifdef COUNTLY_USE_SQLITE
    CHECK(config.databasePath == TEST_DATABASE_NAME);
#endif
    CHECK(config.sessionDuration == 5);
    CHECK(config.eventQueueThreshold == 10);
    CHECK(config.requestQueueThreshold == 10);
    CHECK(config.maxProcessingBatchSize == 10);
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == customSha_1_returnValue);

    HTTPResponse response = config.http_client_function(true, "", "");

    CHECK(response.success);
    CHECK(response.data == "data");

    CHECK(config.metrics["_os"] == "Windows 10");
    CHECK(config.metrics["_os_version"] == "10.22");
    CHECK(config.metrics["_app_version"] == "1.0");
    CHECK(config.metrics["_carrier"] == "Carrier");
    CHECK(config.metrics["_resolution"] == "800x600");
    CHECK(config.metrics["_device"] == "pc");
  }

  // Making sure that after init, none of the configuration values can be changes
  // SDK will be initialised and the state will be validated
  // Afterwards the test will attempt to change the same setters to different values
  // The test would confirm that they can not be set anymore
  SUBCASE("Validating that config values can't be changed after init") {
    clearSDK();
    // setting the initial state
    Countly &ct = Countly::getInstance();
    SHA256Function funPtr = customSha256_1;
    HTTPClientFunction clientPtr = customClient;

    ct.alwaysUsePost(true);
    ct.setDeviceID("test-device-id");
    ct.setSha256(funPtr);
    ct.setHTTPClient(clientPtr);
    ct.SetMetrics("Windows 10", "10.22", "pc", "800x600", "Carrier", "1.0");

    ct.SetMaxEventsPerMessage(10);
    ct.setAutomaticSessionUpdateInterval(5);
    ct.setSalt("salt");
    ct.setMaxRequestQueueSize(10);
    ct.setMaxRQProcessingBatchSize(10);
    ct.SetPath(TEST_DATABASE_NAME);

    // Server and port
    ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);

    CountlyConfiguration config = ct.getConfiguration();

    // Validate configuration values
    CHECK(config.serverUrl == "https://try.count.ly");
    CHECK(config.appKey == "YOUR_APP_KEY");
    CHECK(config.deviceId == "test-device-id");
    CHECK(config.salt == "salt");
#ifdef COUNTLY_USE_SQLITE
    CHECK(config.databasePath == TEST_DATABASE_NAME);
#endif
    CHECK(config.sessionDuration == 5);
    CHECK(config.eventQueueThreshold == 10);
    CHECK(config.requestQueueThreshold == 10);
    CHECK(config.maxProcessingBatchSize == 10);
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == customSha_1_returnValue);

    HTTPResponse response = config.http_client_function(true, "", "");

    CHECK(response.success);
    CHECK(response.data == "data");

    CHECK(config.metrics["_os"] == "Windows 10");
    CHECK(config.metrics["_os_version"] == "10.22");
    CHECK(config.metrics["_app_version"] == "1.0");
    CHECK(config.metrics["_carrier"] == "Carrier");
    CHECK(config.metrics["_resolution"] == "800x600");
    CHECK(config.metrics["_device"] == "pc");

    // trying to change the values after init
    ct.alwaysUsePost(false);
    ct.setSha256(customSha256_2);
    ct.setHTTPClient(new_CustomClient);
    ct.SetMetrics("Windows 11", "10", "p", "800x800", "Car", "1.1");

    ct.setMaxRQProcessingBatchSize(50);
    ct.SetMaxEventsPerMessage(100);
    ct.setAutomaticSessionUpdateInterval(50);
    ct.setSalt("new-salt");
    ct.setMaxRequestQueueSize(100);
    ct.SetPath("new_database.db");

    // get SDK configuration again and make sure that they haven't changed
    config = ct.getConfiguration();
    CHECK(config.serverUrl == "https://try.count.ly");
    CHECK(config.appKey == "YOUR_APP_KEY");
    CHECK(config.deviceId == "test-device-id");
    CHECK(config.salt == "salt");
#ifdef COUNTLY_USE_SQLITE
    CHECK(config.databasePath == TEST_DATABASE_NAME);
#endif
    CHECK(config.sessionDuration == 5);
    CHECK(config.eventQueueThreshold == 10);
    CHECK(config.requestQueueThreshold == 10);
    CHECK(config.maxProcessingBatchSize == 50); // this one should be changed after init
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == customSha_1_returnValue);

    response = config.http_client_function(true, "", "");

    CHECK(response.success);
    CHECK(response.data == "data");

    CHECK(config.metrics["_os"] == "Windows 10");
    CHECK(config.metrics["_os_version"] == "10.22");
    CHECK(config.metrics["_app_version"] == "1.0");
    CHECK(config.metrics["_carrier"] == "Carrier");
    CHECK(config.metrics["_resolution"] == "800x600");
    CHECK(config.metrics["_device"] == "pc");
  }
}
