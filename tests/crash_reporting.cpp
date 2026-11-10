#include <stdexcept>
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
Countly &prepareCrashes() {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  return countly;
}

void startCrashes(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }

/**
 * The crash reports the fake server received, in order.
 */
std::vector<json> sentCrashes(Countly &countly) {
  countly.processRQDebug();
  std::vector<json> crashes;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("crash")) {
      crashes.push_back(json::parse(call.data["crash"]));
    }
  }
  return crashes;
}

long long crashLogCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}
} // namespace

TEST_CASE("A crash takes the breadcrumbs recorded before it, and the next crash starts a new trail") {
  Countly &countly = prepareCrashes();
  startCrashes(countly);
  countly.crash().addBreadcrumb("opened settings");
  countly.crash().recordException("First", "frame 1", false, {{"_os", "Linux"}, {"_app_version", "1.0"}});
  countly.crash().addBreadcrumb("pressed save");
  countly.crash().recordException("Second", "frame 1", false, {{"_os", "Linux"}, {"_app_version", "1.0"}});

  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 2);
  CHECK(crashes[0]["_logs"] == "opened settings\n");
  CHECK(crashes[1]["_logs"] == "pressed save\n");
}

TEST_CASE("clearBreadcrumbs empties the trail") {
  Countly &countly = prepareCrashes();
  startCrashes(countly);
  countly.crash().addBreadcrumb("old step");
  countly.crash().clearBreadcrumbs();
  countly.crash().recordException("Crash", "frame 1", false, {});
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_logs"] == "");
  CHECK(countly.debugHealthCheck()->snapshot()["fu"]["crashes"]["clear"] == 1);
}

TEST_CASE("Crash reports get the device metrics the server needs") {
  Countly &countly = prepareCrashes();
  countly.setMetrics("", "", "Test PC", "1920x1080", "", "3.1");
  startCrashes(countly);
  countly.crash().recordException("No metrics given", "frame 1", true, {});
  countly.crash().recordException("Own metrics", "frame 1", true, {{"_app_version", "9.9"}});

  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 2);
  CHECK(crashes[0]["_os"] == countly.PlatformName());
  CHECK_FALSE(crashes[0]["_os"].get<std::string>().empty());
  CHECK(crashes[0]["_app_version"] == "3.1");
  CHECK(crashes[0]["_device"] == "Test PC");
  CHECK(crashes[0]["_resolution"] == "1920x1080");
  CHECK(crashes[1]["_app_version"] == "9.9");
}

TEST_CASE("A crash without a stack trace is ignored with a warning") {
  Countly &countly = prepareCrashes();
  startCrashes(countly);
  countly.crash().recordException("No stack", "", false, {});
  CHECK(sentCrashes(countly).empty());
  CHECK(crashLogCode(countly, "w700") == 1);
}

TEST_CASE("Crash reports carry the current view") {
  Countly &countly = prepareCrashes();
  startCrashes(countly);
  const std::string viewId = countly.views().openView("Checkout");
  countly.crash().recordException("In checkout", "frame 1", false, {});
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_cvid"] == viewId);
}

TEST_CASE("Global crash segmentation is added to every crash") {
  Countly &countly = prepareCrashes();
  countly.setCustomCrashSegmentation({{"build", "nightly"}, {"region", "eu"}});
  startCrashes(countly);
  countly.crash().recordException("Crash", "frame 1", false, {}, {{"region", "us"}, {"screen", "home"}});
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_custom"] == json({{"build", "nightly"}, {"region", "us"}, {"screen", "home"}}));
}

TEST_CASE("The crash filter sees every crash before it is queued") {
  SUBCASE("it can change the report, and the changed fields are reported in _ob") {
    Countly &countly = prepareCrashes();
    countly.setCrashFilterCallback([](CrashData &crash) {
      crash.stackTrace = "redacted";
      crash.crashSegmentation["email"] = "hidden";
      crash.fatal = !crash.fatal;
      return false;
    });
    startCrashes(countly);
    countly.crash().addBreadcrumb("step");
    countly.crash().recordException("Crash", "frame with jane@example.com", true, {}, {{"email", "jane@example.com"}});
    std::vector<json> crashes = sentCrashes(countly);
    REQUIRE(crashes.size() == 1);
    CHECK(crashes[0]["_error"] == "redacted");
    CHECK(crashes[0]["_custom"]["email"] == "hidden");
    CHECK(crashes[0]["_nonfatal"] == true);
    CHECK(crashes[0]["_ob"] == 25);
  }

  SUBCASE("returning true drops the crash, and its breadcrumbs go with it") {
    Countly &countly = prepareCrashes();
    countly.setCrashFilterCallback([](CrashData &crash) { return crash.description == "Drop me"; });
    startCrashes(countly);
    countly.crash().addBreadcrumb("before the dropped crash");
    countly.crash().recordException("Drop me", "frame 1", false, {});
    countly.crash().recordException("Keep me", "frame 1", false, {});
    std::vector<json> crashes = sentCrashes(countly);
    REQUIRE(crashes.size() == 1);
    CHECK(crashes[0]["_name"] == "Keep me");
    CHECK(crashes[0]["_logs"] == "");
    CHECK(crashes[0]["_ob"] == 0);
  }

  SUBCASE("a filter that throws drops the crash") {
    Countly &countly = prepareCrashes();
    countly.setCrashFilterCallback([](CrashData &) -> bool { throw std::runtime_error("filter failed"); });
    startCrashes(countly);
    countly.crash().recordException("Crash", "frame 1", false, {});
    CHECK(sentCrashes(countly).empty());
  }
}
