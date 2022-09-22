#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>

#include "countly.hpp"
#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"
using namespace test_utils;

using namespace cly;

TEST_CASE("crash unit tests") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  SUBCASE("record crash") {
    // clear the request queue, it contains session begin request
    countly.clearRequestQueue();

    std::map<std::string, std::string> segmentation = {
        {"platform", "ubuntu"},
        {"time", "60"},
    };

    std::map<std::string, std::string> crashMetrics = {
        {"_run", "199222"}, {"_app_version", "1.0"}, {"_disk_current", "654321"}, {"_disk_total", "10585852"}, {"_os_version", "11.1"},
    };

    countly.crash().addBreadcrumb("first");
    countly.crash().addBreadcrumb("second");
    countly.crash().recordException("null pointer exception", "stackTrack", true, crashMetrics, segmentation);

    countly.processRQDebug();

    CHECK(!http_call_queue.empty());
    HTTPCall http_call = http_call_queue.front();

    long long timestamp = getUnixTimestamp();
    long long timestampDiff = timestamp - stoll(http_call.data["timestamp"]);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(timestampDiff >= 0);
    CHECK(timestampDiff <= 1000);

    nlohmann::json c = nlohmann::json::parse(http_call.data["crash"]);

    CHECK(c["_name"].get<std::string>() == "null pointer exception");
    CHECK(c["_error"].get<std::string>() == "stackTrack");
    CHECK(c["_nonfatal"].get<bool>() == false);

    CHECK(c["_run"].get<std::string>() == "199222");
    CHECK(c["_app_version"].get<std::string>() == "1.0");
    CHECK(c["_disk_current"].get<std::string>() == "654321");
    CHECK(c["_disk_total"].get<std::string>() == "10585852");
    CHECK(c["_os_version"].get<std::string>() == "11.1");

    nlohmann::json s = c["_custom"].get<nlohmann::json>();
    CHECK(s["platform"].get<std::string>() == "ubuntu");
    CHECK(s["time"].get<std::string>() == "60");
  }
}
