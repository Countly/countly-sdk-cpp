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

void validateCrashParams(const std::string &title, const std::string &stackTrace, const bool fatal, const std::string &breadCrumbs, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation) {
  CHECK(!http_call_queue.empty());
  HTTPCall http_call = http_call_queue.front();
  long long timestamp = getUnixTimestamp();
  long long timestampDiff = timestamp - std::stoll(http_call.data["timestamp"]);
  CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
  CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
  CHECK(timestampDiff >= 0);
  CHECK(timestampDiff <= 1000);

  nlohmann::json c = nlohmann::json::parse(http_call.data["crash"]);
  CHECK(c["_name"].get<std::string>() == title);
  CHECK(c["_error"].get<std::string>() == stackTrace);
  CHECK(c["_nonfatal"].get<bool>() == !fatal);
  CHECK(c["_logs"].get<std::string>() == breadCrumbs);

  if (!crashMetrics.empty()) {
    for (auto const &metric : crashMetrics) {
      CHECK(c[metric.first].get<std::string>() == metric.second);
    }
  }

  nlohmann::json s = c["_custom"].get<nlohmann::json>();
  if (!segmentation.empty()) {
    for (auto const &segment : segmentation) {
      CHECK(s[segment.first].get<std::string>() == segment.second);
    }
  }

  http_call_queue.pop_front();
}

TEST_CASE("crash unit tests") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  SUBCASE("record crash without bread crumbs") {
    // clear the request queue, it contains session begin request
    countly.clearRequestQueue();

    // crash metrics
    std::map<std::string, std::string> crashMetrics = {
        {"_run", "199222"}, {"_app_version", "1.0"}, {"_disk_current", "654321"}, {"_disk_total", "10585852"}, {"_os", "windows"},
    };

    // record crash with crash metrics
    countly.crash().recordException("Divided By Zero", "stackTrack", true, crashMetrics);

    // start processing request queue
    countly.processRQDebug();

    // validate crash request
    validateCrashParams("Divided By Zero", "stackTrack", true, "", crashMetrics, {});
  }

  SUBCASE("record crash without title, stackTrace, metrics and bread crumbs") {
    // clear the request queue, it contains session begin request
    countly.clearRequestQueue();

    // record fatal exception with empty title and stack trace
    countly.crash().recordException("", "", true, {});

    // start processing request queue
    countly.processRQDebug();

    // validate crash request
    validateCrashParams("", "", true, "", {}, {});
  }

  SUBCASE("record crash with bread crumbs") {
    // clear the request queue, it contains session begin request
    countly.clearRequestQueue();

    // crash segmentation
    std::map<std::string, std::string> segmentation = {
        {"platform", "ubuntu"},
        {"time", "60"},
    };

    // crash metrics
    std::map<std::string, std::string> crashMetrics = {
        {"_run", "199222"}, {"_app_version", "1.0"}, {"_disk_current", "654321"}, {"_disk_total", "10585852"}, {"_os_version", "11.1"},
    };

    // leave breadcrumb
    countly.crash().addBreadcrumb("first");
    countly.crash().recordException("null pointer exception", "stackTrack", false, crashMetrics, segmentation);

    // start processing request queue
    countly.processRQDebug();

    // validate crash request
    validateCrashParams("null pointer exception", "stackTrack", false, "first\n", crashMetrics, {});

    // leave breadcrumb
    countly.crash().addBreadcrumb("second");

    // record crash with crash metrics and segmentation
    countly.crash().recordException("Divided By Zero", "stackTrack", true, crashMetrics, segmentation);

    // start processing request queue
    countly.processRQDebug();

    // validate crash request
    validateCrashParams("Divided By Zero", "stackTrack", true, "first\nsecond\n", crashMetrics, segmentation);
  }
}
