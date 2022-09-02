#include "countly.hpp"
#include "doctest.h"
using namespace cly;

TEST_CASE("validate configuration all setters") {

  Countly &ct = Countly::getInstance();
  SUBCASE("default values") {
    const CountlyConfiguration *config = ct.getConfiguration();
    CHECK(config->serverUrl == "");
    CHECK(config->appKey == "");
    CHECK(config->deviceId == "");
    CHECK(config->salt == "");
    CHECK(config->sessionDuration == 60);
    CHECK(config->eventQueueThreshold == 100);
    CHECK(config->requestQueueThreshold == 1000);
    CHECK(config->breadcrumbsThreshold == 100);
    CHECK(config->enablePost == false);
    CHECK(config->port == 443);
    CHECK(config->sha256_function == nullptr);
    CHECK(config->http_client_function == nullptr);
  }

  ct.alwaysUsePost(true);
  ct.setDeviceID("test-device-id");

  // ct.setLogger(printLog);
  //  OS, OS_version, device, resolution, carrier, app_version);
  ct.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
  // Server and port
  // ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, true);
  ct.SetMaxEventsPerMessage(10);
  ct.setAutomaticSessionUpdateInterval(5);
  ct.setSalt("salt");

  SUBCASE("validate values") {
    const CountlyConfiguration *config = ct.getConfiguration();
    // CHECK(config->serverUrl == "https://try.count.ly");
    // CHECK(config->appKey == "YOUR_APP_KEY");
    CHECK(config->deviceId == "test-device-id");
    CHECK(config->salt == "salt");
    CHECK(config->sessionDuration == 5);
    CHECK(config->eventQueueThreshold == 10);
    CHECK(config->requestQueueThreshold == 1000);
    CHECK(config->breadcrumbsThreshold == 100);
    CHECK(config->enablePost == true);
    CHECK(config->port == 443);
    CHECK(config->sha256_function == nullptr);
    CHECK(config->http_client_function == nullptr);
  }
}
