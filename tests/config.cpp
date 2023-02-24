#include "countly.hpp"
#include "doctest.h"
#include "test_utils.hpp"
using namespace cly;
using namespace test_utils;

std::string customSha256(const std::string &data) { return "SHA256"; }
std::string new_CustomSha256(const std::string &data) { return "new-SHA256"; }

HTTPResponse customClient(bool f, const std::string &a, const std::string &b) {
  HTTPResponse response;
  response.success = true;
  response.data = "data";

  return response;
}

HTTPResponse new_CustomClient(bool f, const std::string &a, const std::string &b) {
  HTTPResponse response;
  response.success = false;
  response.data = "new-data";

  return response;
}

TEST_CASE("validate configuration all setters") {
  SUBCASE("default values") {
    clearSDK();
    Countly &ct = Countly::getInstance();
    const CountlyConfiguration config = ct.getConfiguration();
    CHECK(config.serverUrl == "");
    CHECK(config.appKey == "");
    CHECK(config.deviceId == "");
    CHECK(config.salt == "");
    CHECK(config.databasePath == "");

    CHECK(config.sessionDuration == 60);
    CHECK(config.eventQueueThreshold == 100);
    CHECK(config.requestQueueThreshold == 1000);
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == false);
    CHECK(config.port == 443);
    CHECK(config.sha256_function == nullptr);
    CHECK(config.http_client_function == nullptr);

    CHECK(config.metrics.empty());
  }

  SUBCASE("validate values") {
    clearSDK();
    Countly &ct = Countly::getInstance();
    SHA256Function funPtr = customSha256;
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
    ct.SetPath(TEST_DATABASE_NAME);

    // Server and port
    ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);

    CountlyConfiguration config = ct.getConfiguration();

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
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == "SHA256");

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

  SUBCASE("validate set configuration after SDK init") {
    clearSDK();
    Countly &ct = Countly::getInstance();
    SHA256Function funPtr = customSha256;
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
    ct.SetPath(TEST_DATABASE_NAME);

    // Server and port
    ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);

    CountlyConfiguration config = ct.getConfiguration();
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
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == "SHA256");

    HTTPResponse response = config.http_client_function(true, "", "");

    CHECK(response.success);
    CHECK(response.data == "data");

    CHECK(config.metrics["_os"] == "Windows 10");
    CHECK(config.metrics["_os_version"] == "10.22");
    CHECK(config.metrics["_app_version"] == "1.0");
    CHECK(config.metrics["_carrier"] == "Carrier");
    CHECK(config.metrics["_resolution"] == "800x600");
    CHECK(config.metrics["_device"] == "pc");

    ct.alwaysUsePost(false);
    ct.setSha256(new_CustomSha256);
    ct.setHTTPClient(new_CustomClient);
    ct.SetMetrics("Windows 11", "10", "p", "800x800", "Car", "1.1");

    ct.SetMaxEventsPerMessage(100);
    ct.setAutomaticSessionUpdateInterval(50);
    ct.setSalt("new-salt");
    ct.setMaxRequestQueueSize(100);
    ct.SetPath("new_database.db");

    // get SDK configuration again.
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
    CHECK(config.breadcrumbsThreshold == 100);
    CHECK(config.forcePost == true);
    CHECK(config.port == 443);
    CHECK(config.sha256_function("custom SHA256") == "SHA256");

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
