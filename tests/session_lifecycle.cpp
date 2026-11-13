#include <atomic>
#include <chrono>
#include <functional>
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
/**
 * What a request did, in a word: begin, update, end, events or user_details. Empty for other requests.
 */
std::string requestKind(const HTTPCall &call) {
  if (call.data.count("begin_session")) {
    return "begin";
  }
  if (call.data.count("end_session")) {
    return "end";
  }
  if (call.data.count("session_duration")) {
    return "update";
  }
  if (call.data.count("events")) {
    return "events";
  }
  if (call.data.count("user_details")) {
    return "user_details";
  }
  return "";
}

/**
 * The kinds of the queued requests the fake server received, in order.
 */
std::vector<std::string> receivedKinds() {
  std::vector<std::string> kinds;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    const std::string kind = requestKind(http_call_queue.at(index));
    if (!kind.empty()) {
      kinds.push_back(kind);
    }
  }
  return kinds;
}

bool received(const std::string &kind) {
  for (const std::string &each : receivedKinds()) {
    if (each == kind) {
      return true;
    }
  }
  return false;
}

/**
 * Polls until the condition holds or the timeout passes.
 */
bool eventually(std::function<bool()> condition, std::chrono::milliseconds timeout) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return condition();
}

long long sessionLogCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

Countly &prepareSessions() {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  return countly;
}

void startSessions(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }

std::atomic<bool> release_settings{false};

/**
 * Answers the behavior settings fetch with session tracking off once the test releases it.
 */
HTTPResponse delayedSessionTrackingOff(bool use_post, const std::string &url, const std::string &data) {
  if (url == "/o/sdk" && data.find("method=sc") != std::string::npos) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!release_settings.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return HTTPResponse{true, json::parse(R"({"v":2,"t":1,"c":{"st":false}})"), 200};
  }
  return fakeSendHTTP(use_post, url, data);
}
} // namespace

TEST_CASE("Sessions are updated on the update timer and events are sent on every pass of the update loop") {
  SUBCASE("also when start() is called without its last parameter") {
    Countly &countly = prepareSessions();
    countly.setAutomaticSessionUpdateInterval(1);
    countly.setUpdateInterval(100);
    startSessions(countly);
    countly.RecordEvent("tap", 1);

    CHECK(eventually([] { return received("update") && received("events"); }, std::chrono::seconds(4)));
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("events go out on the next pass of the update loop, before the session update is due") {
    Countly &countly = prepareSessions();
    countly.setAutomaticSessionUpdateInterval(60);
    countly.setUpdateInterval(100);
    startSessions(countly);
    countly.RecordEvent("tap", 1);

    CHECK(eventually([] { return received("events"); }, std::chrono::milliseconds(1500)));
    CHECK(countly.checkEQSize() == 0);
    CHECK_FALSE(received("update"));
  }

  SUBCASE("with manual sessions the update timer still sends the events") {
    Countly &countly = prepareSessions();
    countly.enableManualSessionControl();
    countly.setAutomaticSessionUpdateInterval(1);
    countly.setUpdateInterval(100);
    startSessions(countly);
    countly.RecordEvent("tap", 1);

    CHECK(eventually([] { return received("events"); }, std::chrono::seconds(4)));
    CHECK_FALSE(received("update"));
  }
}

TEST_CASE("Session calls of the app are ignored while sessions are automatic") {
  Countly &countly = prepareSessions();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  startSessions(countly);
  countly.processRQDebug();
  http_call_queue.clear();

  CHECK_FALSE(countly.beginSession());
  CHECK_FALSE(countly.updateSession());
  CHECK_FALSE(countly.endSession());
  countly.processRQDebug();

  CHECK(receivedKinds().empty());
  CHECK(sessionLogCode(countly, "w740") == 3);
  CHECK(countly.IsSessionRunning());
}

TEST_CASE("Ending a session sends the waiting events first") {
  Countly &countly = prepareSessions();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  startSessions(countly);
  countly.beginSession();
  countly.RecordEvent("inside_the_session", 1);
  countly.endSession();
  countly.processRQDebug();

  CHECK(receivedKinds() == std::vector<std::string>{"begin", "events", "end"});
}

TEST_CASE("stop() ends the session with its events before it stops") {
  Countly &countly = prepareSessions();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  startSessions(countly);
  countly.RecordEvent("last_tap", 1);
  countly.stop();
  countly.processRQDebug();

  CHECK(receivedKinds() == std::vector<std::string>{"begin", "events", "end"});
}

TEST_CASE("While the server turns session tracking off") {
  SUBCASE("saving user details neither blocks nor drops the waiting events") {
    Countly &countly = prepareSessions();
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    countly.setSDKBehaviorSettings("{\"st\":false}");
    startSessions(countly);
    countly.RecordEvent("before_details", 1);

    const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    countly.setUserDetails({{"name", "Alice"}});
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));

    countly.processRQDebug();
    CHECK(receivedKinds() == std::vector<std::string>{"events", "user_details"});
  }

  SUBCASE("a running session still ends") {
    clearSDK();
    release_settings = false;
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(delayedSessionTrackingOff);
    countly.disableHealthCheck();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableManualSessionControl();
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    startSessions(countly);
    countly.beginSession();
    release_settings = true;

    // once the settings arrived, a new session can't begin
    REQUIRE(eventually(
        [&countly] {
          countly.endSession();
          countly.beginSession();
          return !countly.IsSessionRunning();
        },
        std::chrono::seconds(5)));
    countly.processRQDebug();
    std::vector<std::string> kinds = receivedKinds();
    REQUIRE(!kinds.empty());
    CHECK(kinds.back() == "end");
  }
}

TEST_CASE("The first view of every session is marked") {
  Countly &countly = prepareSessions();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  startSessions(countly);

  countly.views().openView("BeforeAnySession");
  countly.beginSession();
  countly.views().openView("FirstOfFirstSession");
  countly.views().openView("SecondOfFirstSession");
  countly.endSession();
  countly.beginSession();
  countly.views().openView("FirstOfSecondSession");
  countly.flushEvents();
  countly.processRQDebug();

  std::map<std::string, bool> marked;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("events")) {
      for (const json &event : json::parse(call.data["events"])) {
        if (event["key"] == "[CLY]_view" && event["segmentation"].contains("visit")) {
          marked[event["segmentation"]["name"].get<std::string>()] = event["segmentation"].contains("start");
        }
      }
    }
  }
  CHECK(marked == std::map<std::string, bool>{{"BeforeAnySession", false}, {"FirstOfFirstSession", true}, {"SecondOfFirstSession", false}, {"FirstOfSecondSession", true}});
}
