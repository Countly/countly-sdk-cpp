#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
std::mutex rc_mutex;
std::vector<std::map<std::string, std::string>> rc_requests;
std::atomic<bool> hold_rc_answers{false};
std::atomic<int> rc_answer_number{0};

/**
 * Records the remote config downloads and answers each with a value telling which download it was. Holds the
 * answers back while hold_rc_answers is set.
 */
HTTPResponse remoteConfigClient(bool use_post, const std::string &url, const std::string &data) {
  HTTPResponse response = fakeSendHTTP(use_post, url, data);
  if (url != "/o/sdk" || data.find("method=rc") == std::string::npos) {
    return response;
  }
  std::map<std::string, std::string> params;
  std::string::size_type start = 0;
  while (start < data.size()) {
    std::string::size_type end = data.find('&', start);
    if (end == std::string::npos) {
      end = data.size();
    }
    const std::string pair = data.substr(start, end - start);
    const std::string::size_type equals = pair.find('=');
    std::string value = equals == std::string::npos ? std::string() : pair.substr(equals + 1);
    decodeURL(value);
    params[pair.substr(0, equals)] = value;
    start = end + 1;
  }
  {
    std::lock_guard<std::mutex> lock(rc_mutex);
    rc_requests.push_back(params);
  }
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (hold_rc_answers.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const int number = ++rc_answer_number;
  return HTTPResponse{true, json({{"color", "red"}, {"download", number}}), 200, ""};
}

std::vector<std::map<std::string, std::string>> rcRequests() {
  std::lock_guard<std::mutex> lock(rc_mutex);
  return rc_requests;
}

bool waitUntilTrue(std::function<bool()> condition) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return condition();
}

Countly &prepareRemoteConfig() {
  clearSDK();
  {
    std::lock_guard<std::mutex> lock(rc_mutex);
    rc_requests.clear();
  }
  hold_rc_answers = false;
  rc_answer_number = 0;
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(remoteConfigClient);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setMetrics("", "", "", "", "", "2.0");
  return countly;
}

void startRemoteConfig(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }
} // namespace

TEST_CASE("Remote config downloads use the rc method") {
  SUBCASE("with the device metrics, enrolling nobody in A/B tests by default") {
    Countly &countly = prepareRemoteConfig();
    startRemoteConfig(countly);
    countly.updateRemoteConfig();
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    std::map<std::string, std::string> request = rcRequests()[0];
    CHECK(request["method"] == "rc");
    CHECK_FALSE(request.count("oi"));
    CHECK_FALSE(request.count("keys"));
    json metrics = json::parse(request["metrics"]);
    CHECK(metrics["_app_version"] == "2.0");
    CHECK(metrics.contains("_os"));
    CHECK(request["t"] == "0");
  }

  SUBCASE("enrolling in A/B tests when the app asks for it") {
    Countly &countly = prepareRemoteConfig();
    countly.enrollABOnRCDownload();
    startRemoteConfig(countly);
    countly.updateRemoteConfig();
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    CHECK(rcRequests()[0]["oi"] == "1");
  }

  SUBCASE("asking for some keys, or for all but some") {
    Countly &countly = prepareRemoteConfig();
    startRemoteConfig(countly);
    std::string keys[] = {"color"};
    countly.updateRemoteConfigFor(keys, 1);
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    countly.updateRemoteConfigExcept(keys, 1);
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 2; }));
    CHECK(rcRequests()[0]["keys"] == "[\"color\"]");
    CHECK(rcRequests()[1]["omit_keys"] == "[\"color\"]");
  }
}

TEST_CASE("Remote config values belong to the user they were downloaded for") {
  SUBCASE("a device ID change without merge clears them") {
    Countly &countly = prepareRemoteConfig();
    startRemoteConfig(countly);
    countly.updateRemoteConfig();
    REQUIRE(waitUntilTrue([&countly] { return countly.getRemoteConfigValue("color") == "red"; }));
    countly.setDeviceID("second-user", false);
    CHECK(countly.getRemoteConfigValue("color").is_null());
  }

  SUBCASE("a device ID change with merge clears them") {
    Countly &countly = prepareRemoteConfig();
    startRemoteConfig(countly);
    countly.updateRemoteConfig();
    REQUIRE(waitUntilTrue([&countly] { return countly.getRemoteConfigValue("color") == "red"; }));
    countly.setDeviceID("merged-user", true);
    CHECK(countly.getRemoteConfigValue("color").is_null());
  }

  SUBCASE("an answer that arrives after a device ID change is discarded") {
    Countly &countly = prepareRemoteConfig();
    startRemoteConfig(countly);
    hold_rc_answers = true;
    countly.updateRemoteConfig();
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    countly.setDeviceID("second-user", false);
    hold_rc_answers = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(countly.getRemoteConfigValue("color").is_null());
  }
}

TEST_CASE("Automatic remote config downloads") {
  SUBCASE("happen at the end of start(), not at every session start") {
    Countly &countly = prepareRemoteConfig();
    countly.enableRemoteConfig();
    startRemoteConfig(countly);
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    countly.beginSession();
    countly.endSession();
    countly.beginSession();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(rcRequests().size() == 1);
  }

  SUBCASE("follow a device ID change without merge, also when a download for the previous user is running") {
    Countly &countly = prepareRemoteConfig();
    countly.enableRemoteConfig();
    hold_rc_answers = true;
    startRemoteConfig(countly);
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    countly.setDeviceID("second-user", false);
    hold_rc_answers = false;
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 2; }));
    CHECK(rcRequests()[1]["device_id"] == "second-user");
    CHECK(waitUntilTrue([&countly] { return countly.getRemoteConfigValue("download") == 2; }));
  }

  SUBCASE("follow a device ID change with merge once the merge request was sent") {
    Countly &countly = prepareRemoteConfig();
    countly.enableRemoteConfig();
    startRemoteConfig(countly);
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 1; }));
    countly.setDeviceID("merged-user", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(rcRequests().size() == 1);
    countly.processRQDebug();
    REQUIRE(waitUntilTrue([] { return rcRequests().size() == 2; }));
    CHECK(rcRequests()[1]["device_id"] == "merged-user");
  }
}
