#include "countly.hpp"
#include "doctest.h"
using namespace cly;
std::string customSha256(const std::string &data) { return "SHA256"; }

HTTPResponse customClient(bool f, const std::string &a, const std::string &b) {
  HTTPResponse response;
  response.success = true;
  response.data = "data";

  return response;
}

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

    CHECK(config->metrics.os.empty());
    CHECK(config->metrics.osVersion.empty());
    CHECK(config->metrics.appVersion.empty());
    CHECK(config->metrics.carrier.empty());
    CHECK(config->metrics.resolution.empty());
    CHECK(config->metrics.device.empty());
  }

  SHA256Function funPtr = customSha256;
  HTTPClientFunction clientPtr = customClient;

  ct.alwaysUsePost(true);
  ct.setDeviceID("test-device-id");
  ct.setSha256(funPtr);
  ct.setHTTPClient(clientPtr);
  ct.SetMetrics("Windows 10", "10.22", "pc", "800x600", "Carrier", "1.0");
  // Server and port
  ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);
  ct.SetMaxEventsPerMessage(10);
  ct.setAutomaticSessionUpdateInterval(5);
  ct.setSalt("salt");

  SUBCASE("validate values") {
    const CountlyConfiguration *config = ct.getConfiguration();
    CHECK(config->serverUrl == "https://try.count.ly");
    CHECK(config->appKey == "YOUR_APP_KEY");
    CHECK(config->deviceId == "test-device-id");
    CHECK(config->salt == "salt");
    CHECK(config->sessionDuration == 5);
    CHECK(config->eventQueueThreshold == 10);
    CHECK(config->requestQueueThreshold == 1000);
    CHECK(config->breadcrumbsThreshold == 100);
    CHECK(config->enablePost == true);
    CHECK(config->port == 443);
    CHECK(config->sha256_function("custom SHA256") == "SHA256");

    HTTPResponse response = config->http_client_function(true, "", "");

    CHECK(response.success);
    CHECK(response.data == "data");

    CHECK(config->metrics.os == "Windows 10");
    CHECK(config->metrics.osVersion == "10.22");
    CHECK(config->metrics.appVersion == "1.0");
    CHECK(config->metrics.carrier == "Carrier");
    CHECK(config->metrics.resolution == "800x600");
    CHECK(config->metrics.device == "pc");
  }

  Countly::reset();
}
