#include <chrono>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

/**
 * Waits until start() has sent the SDK behavior settings request from its own thread,
 * so it cannot land among the requests a test asserts on.
 */
static void waitForConfigFetch() {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!httpQueueContains("method", "sc")) {
    if (std::chrono::steady_clock::now() >= deadline) {
      FAIL("start() did not send the SDK behavior settings request");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

/**
 * Starts the SDK with manual session control and the given SDK behavior settings and HTTP client,
 * then drops what start() sent, so a test sees only the requests it causes.
 */
static void startForUserDetails(Countly &countly, const json &sbs = json::object(), HTTPClientFunction client = test_utils::fakeSendHTTP) {
  if (!sbs.empty()) {
    std::string settings = sbs.dump();
    countly.setSDKBehaviorSettings(settings);
  }
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setHTTPClient(client);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableManualSessionControl();
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  waitForConfigFetch();
  countly.clearRequestQueue();
  http_call_queue.clear();
}

/**
 * Removes the oldest captured request and returns it.
 */
static HTTPCall popRequest() {
  REQUIRE(!http_call_queue.empty());
  HTTPCall call = http_call_queue.front();
  http_call_queue.pop_front();
  return call;
}

/**
 * Returns the captured requests that carry the given parameter, oldest first.
 */
static std::vector<HTTPCall> requestsWith(const std::string &parameter) {
  std::vector<HTTPCall> calls;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.find(parameter) != call.data.end()) {
      calls.push_back(call);
    }
  }
  return calls;
}

/**
 * The user_details a request carries when it multiplies the custom property 'key' by 'value'.
 */
static json multiplied(const std::string &key, double value) { return json{{"custom", {{key, {{"$mul", value}}}}}}; }

/**
 * HTTP client that captures each request like fakeSendHTTP, but reports that the server did not accept it.
 */
static HTTPResponse rejectingSendHTTP(bool use_post, const std::string &url, const std::string &data) {
  fakeSendHTTP(use_post, url, data);
  return HTTPResponse{false, json::object()};
}

TEST_CASE("multiplyCustomUserDetail sends the $mul modifier") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  startForUserDetails(countly);

  SUBCASE("a decimal multiplier is sent for the current device ID") {
    countly.multiplyCustomUserDetail("score", 2.5);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    HTTPCall call = popRequest();
    CHECK(call.url == "/i");
    CHECK(call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(json::parse(call.data["user_details"]) == multiplied("score", 2.5));
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("integer, zero and negative multipliers are each sent as their own request, in call order") {
    countly.multiplyCustomUserDetail("level", 3);
    countly.multiplyCustomUserDetail("level", 0);
    countly.multiplyCustomUserDetail("balance", -1.5);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 3);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("level", 3));
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("level", 0));
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("balance", -1.5));
  }
}

TEST_CASE("multiplyCustomUserDetail and recorded events") {
  SUBCASE("recorded events are sent before the modifier") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startForUserDetails(countly);

    generateEvents(2, countly);
    REQUIRE(countly.checkEQSize() == 2);
    countly.multiplyCustomUserDetail("score", 2);
    CHECK(countly.checkEQSize() == 0);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 2);
    HTTPCall events = popRequest();
    CHECK(json::parse(events.data["events"]).size() == 2);
    CHECK(events.data.find("user_details") == events.data.end());
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 2));
  }

  SUBCASE("recorded events stay queued after disableAutoEventsOnUserProperties") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.disableAutoEventsOnUserProperties();
    startForUserDetails(countly);

    generateEvents(2, countly);
    countly.multiplyCustomUserDetail("score", 2);
    CHECK(countly.checkEQSize() == 2);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 2));
  }
}

TEST_CASE("multiplyCustomUserDetail ignores what it cannot send") {
  SUBCASE("a call before start is neither sent nor kept for the first session") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = Countly::getInstance();
    countly.setLogger(captureLog);
    countly.setHTTPClient(test_utils::fakeSendHTTP);
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);

    countly.multiplyCustomUserDetail("score", 2);
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    waitForConfigFetch();
    countly.multiplyCustomUserDetail("score", 3);
    countly.processRQDebug();

    CHECK(capturedLogContains("multiplyCustomUserDetail, SDK is not initialized"));
    std::vector<HTTPCall> sessions = requestsWith("begin_session");
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].data.find("user_details") == sessions[0].data.end());
    std::vector<HTTPCall> userDetails = requestsWith("user_details");
    REQUIRE(userDetails.size() == 1);
    CHECK(json::parse(userDetails[0].data["user_details"]) == multiplied("score", 3));
  }

  SUBCASE("an empty key is not sent") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = Countly::getInstance();
    countly.setLogger(captureLog);
    startForUserDetails(countly);

    countly.multiplyCustomUserDetail("", 2);
    countly.multiplyCustomUserDetail("score", 3);
    countly.processRQDebug();

    CHECK(capturedLogContains("multiplyCustomUserDetail, The parameter 'key' can't be empty"));
    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 3));
  }

  SUBCASE("a multiplier that is not a finite number is not sent") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = Countly::getInstance();
    countly.setLogger(captureLog);
    startForUserDetails(countly);

    countly.multiplyCustomUserDetail("nan", std::numeric_limits<double>::quiet_NaN());
    countly.multiplyCustomUserDetail("infinity", std::numeric_limits<double>::infinity());
    countly.multiplyCustomUserDetail("negative_infinity", -std::numeric_limits<double>::infinity());
    countly.multiplyCustomUserDetail("score", 3);
    countly.processRQDebug();

    CHECK(capturedLogContains("multiplyCustomUserDetail, The parameter 'value' must be a finite number"));
    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 3));
  }
}

TEST_CASE("multiplyCustomUserDetail follows the SDK behavior settings") {
  SUBCASE("a key on the user property blacklist is not sent") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startForUserDetails(countly, {{"upb", json::array({"blocked"})}});

    countly.multiplyCustomUserDetail("blocked", 2);
    countly.multiplyCustomUserDetail("allowed", 3);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("allowed", 3));
  }

  SUBCASE("only a key on the user property whitelist is sent") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startForUserDetails(countly, {{"upw", json::array({"allowed"})}});

    countly.multiplyCustomUserDetail("other", 2);
    countly.multiplyCustomUserDetail("allowed", 3);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("allowed", 3));
  }

  SUBCASE("the key is truncated to the maximum key length") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startForUserDetails(countly, {{"lkl", 3}});

    countly.multiplyCustomUserDetail("longkey", 2);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("lon", 2));
  }

  SUBCASE("a key the maximum key length cuts down to nothing is not sent") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startForUserDetails(countly, {{"lkl", 1}});

    countly.multiplyCustomUserDetail("\xC3\xA9score", 2);
    countly.multiplyCustomUserDetail("s", 3);
    countly.processRQDebug();

    REQUIRE(http_call_queue.size() == 1);
    CHECK(json::parse(popRequest().data["user_details"]) == multiplied("s", 3));
  }
}

TEST_CASE("multiplyCustomUserDetail sends details still waiting from before start ahead of the modifier") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setUserDetails({{"name", "First User"}});
  countly.setCustomUserDetails({{"score", "10"}});
  startForUserDetails(countly);

  countly.multiplyCustomUserDetail("score", 2);
  countly.beginSession();
  countly.processRQDebug();

  REQUIRE(http_call_queue.size() == 3);
  HTTPCall waiting = popRequest();
  CHECK(waiting.data.find("begin_session") == waiting.data.end());
  CHECK(json::parse(waiting.data["user_details"]) == json({{"name", "First User"}, {"custom", {{"score", "10"}}}}));
  CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 2));
  HTTPCall session = popRequest();
  CHECK(session.data["begin_session"] == "1");
  CHECK(session.data.find("user_details") == session.data.end());
}

TEST_CASE("multiplyCustomUserDetail keeps its request for a retry when the server does not accept it") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  startForUserDetails(countly, json::object(), rejectingSendHTTP);

  countly.multiplyCustomUserDetail("score", 2);
  countly.processRQDebug();

  REQUIRE(http_call_queue.size() == 1);
  CHECK(json::parse(popRequest().data["user_details"]) == multiplied("score", 2));
  CHECK(countly.checkRQSize() == 1);
}
