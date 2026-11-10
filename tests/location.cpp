#include <string>
#include <vector>

#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
Countly &prepareLocation(bool manualSessions = false) {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  if (manualSessions) {
    countly.enableManualSessionControl();
  }
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  return countly;
}

void startLocation(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }

/**
 * The location parameters of each request the fake server received, with "begin" for a session start. Requests
 * without a location parameter are left out.
 */
std::vector<std::map<std::string, std::string>> locationRequests(Countly &countly) {
  countly.processRQDebug();
  std::vector<std::map<std::string, std::string>> requests;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    std::map<std::string, std::string> location;
    for (const char *key : {"country_code", "city", "location", "ip_address", "begin_session"}) {
      if (call.data.count(key)) {
        location[key] = call.data[key];
      }
    }
    if (location.size() > (location.count("begin_session") ? 1u : 0u) || location.count("begin_session")) {
      requests.push_back(location);
    }
  }
  http_call_queue.clear();
  return requests;
}

long long locationLogCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}
} // namespace

TEST_CASE("A location given before start() goes with the first session") {
  Countly &countly = prepareLocation();
  countly.setLocation("DE", "Berlin", "52.52001,13.40495", "203.0.113.7");
  startLocation(countly);
  std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == std::map<std::string, std::string>{{"begin_session", "1"}, {"country_code", "DE"}, {"city", "Berlin"}, {"location", "52.52001,13.40495"}, {"ip_address", "203.0.113.7"}});
}

TEST_CASE("A separate location request leaves out the IP address") {
  Countly &countly = prepareLocation();
  startLocation(countly);
  locationRequests(countly);
  countly.setLocation("DE", "Berlin", "52.52001,13.40495", "203.0.113.7");
  std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == std::map<std::string, std::string>{{"country_code", "DE"}, {"city", "Berlin"}, {"location", "52.52001,13.40495"}});
}

TEST_CASE("Location values that don't fit their form are not used") {
  Countly &countly = prepareLocation();
  startLocation(countly);
  locationRequests(countly);
  countly.setLocation("us", "Boston", "north,south", "not-an-ip");
  std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == std::map<std::string, std::string>{{"city", "Boston"}});
  CHECK(locationLogCode(countly, "w700") == 3);
}

TEST_CASE("A setLocation call without any value only clears the stored values") {
  Countly &countly = prepareLocation(true);
  countly.setLocation("DE", "Berlin", "", "");
  startLocation(countly);
  countly.setLocation("", "", "", "");
  CHECK(locationRequests(countly).empty());
  countly.beginSession();
  std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == std::map<std::string, std::string>{{"begin_session", "1"}});
}

TEST_CASE("disableLocation turns location off") {
  SUBCASE("after start: the values are cleared, an erase request goes out and sessions carry the empty location") {
    Countly &countly = prepareLocation(true);
    startLocation(countly);
    countly.setLocation("DE", "Berlin", "", "");
    countly.disableLocation();
    countly.beginSession();
    std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(requests[0] == std::map<std::string, std::string>{{"location", ""}});
    CHECK(requests[1] == std::map<std::string, std::string>{{"begin_session", "1"}, {"location", ""}});

    // a location given while no session runs waits for the next session start, and turns location back on
    countly.endSession();
    countly.setLocation("FR", "Paris", "", "");
    countly.beginSession();
    requests = locationRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == std::map<std::string, std::string>{{"begin_session", "1"}, {"country_code", "FR"}, {"city", "Paris"}});
  }

  SUBCASE("before start: the location given before start is not used") {
    Countly &countly = prepareLocation();
    countly.setLocation("DE", "Berlin", "", "");
    countly.disableLocation();
    startLocation(countly);
    std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == std::map<std::string, std::string>{{"begin_session", "1"}, {"location", ""}});
  }

  SUBCASE("the server's location setting wins over the app's choice") {
    Countly &countly = prepareLocation();
    countly.setSDKBehaviorSettings("{\"lt\":true}");
    countly.disableLocation();
    startLocation(countly);
    std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == std::map<std::string, std::string>{{"begin_session", "1"}});
  }
}

TEST_CASE("While the server turns location off, setLocation is ignored") {
  Countly &countly = prepareLocation();
  countly.setSDKBehaviorSettings("{\"lt\":false}");
  startLocation(countly);
  locationRequests(countly);
  countly.setLocation("DE", "Berlin", "", "");
  CHECK(locationRequests(countly).empty());
  json hc = countly.debugHealthCheck()->snapshot();
  CHECK(hc["el"] == 0);
}

TEST_CASE("At the end of start(), an erase request goes out when location is off and no session starts") {
  Countly &countly = prepareLocation();
  countly.setSDKBehaviorSettings("{\"st\":false}");
  countly.disableLocation();
  startLocation(countly);
  std::vector<std::map<std::string, std::string>> requests = locationRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == std::map<std::string, std::string>{{"location", ""}});
}
