#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"
using namespace test_utils;

using namespace cly;
using namespace std::literals::chrono_literals;

HTTPCall popHTTPCall() {
  CHECK(!http_call_queue.empty());
  HTTPCall oldest_call = http_call_queue.front();
  http_call_queue.pop_front();
  return oldest_call;
}

TEST_CASE("sessions unit tests") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.setAutomaticSessionUpdateInterval(2);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  // Wait for the async SBS config fetch thread to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  SUBCASE("init sdk - session begin ") {
    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall(); // first request is SBS
    http_call = popHTTPCall();      // second request is session begin
    long long timestamp = getUnixTimestamp();
    long long timestampDiff = timestamp - stoll(http_call.data["timestamp"]);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(http_call.data["begin_session"] == "1");
    CHECK(timestampDiff >= 0);
    CHECK(timestampDiff <= 50000);
  }

  SUBCASE("session update ") {
    countly.processRQDebug();
    countly.clearRequestQueue(); // request queue contains session begin request
    http_call_queue.clear();     // clear local HTTP request queue.

    std::this_thread::sleep_for(3s);
    countly.updateSession();
    countly.processRQDebug();

    HTTPCall http_call = popHTTPCall();
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(!http_call.data["session_duration"].empty());
  }

  SUBCASE("session end ") {
    countly.processRQDebug();
    countly.clearRequestQueue(); // request queue contains session begin request
    http_call_queue.clear();     // clear local HTTP request queue.

    countly.endSession();
    countly.processRQDebug();

    HTTPCall http_call = popHTTPCall();
    long long timestamp = getUnixTimestamp();
    long long timestampDiff = timestamp - stoll(http_call.data["timestamp"]);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(!http_call.data["session_duration"].empty());
    CHECK(http_call.data["end_session"] == "1");
    CHECK(timestampDiff >= 0);
    CHECK(timestampDiff <= 1000);
  }
}

TEST_CASE("event request unit tests") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);

  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  // Wait for the async SBS config fetch thread to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("single event is sent") {
    cly::Event event("win", 4);
    event.addSegmentation("points", 100);
    countly.addEvent(event);

    countly.updateSession();
    countly.processRQDebug();

    HTTPCall http_call = popHTTPCall();

    CHECK(!http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);

    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    nlohmann::json e = events[0];
    CHECK(e["key"].get<std::string>() == "win");
    CHECK(e["count"].get<int>() == 4);
    CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

    nlohmann::json s = e["segmentation"].get<nlohmann::json>();
    CHECK(s["points"].get<int>() == 100);
  }

  SUBCASE("two events are sent") {
    cly::Event event1("win", 2);
    cly::Event event2("achievement", 1);
    countly.addEvent(event1);
    countly.addEvent(event2);

    countly.updateSession();

    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall();
    CHECK(!http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);

    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    nlohmann::json e = events[0];
    CHECK(e["key"].get<std::string>() == "win");
    CHECK(e["count"].get<int>() == 2);
    CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

    e = events[1];
    CHECK(e["key"].get<std::string>() == "achievement");
    CHECK(e["count"].get<int>() == 1);
    CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);
  }

  SUBCASE("event with count, sum, duration and segmentation is sent") {
    cly::Event event("lose", 3, 10, 100);
    event.addSegmentation("points", 2000);
    countly.addEvent(event);

    countly.updateSession();

    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall();
    CHECK(!http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);

    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    nlohmann::json e = events[0];
    CHECK(e["key"].get<std::string>() == "lose");
    CHECK(e["count"].get<int>() == 3);
    CHECK(e["sum"].get<double>() == 10.0);
    CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

    nlohmann::json s = e["segmentation"].get<nlohmann::json>();
    CHECK(s["points"].get<int>() == 2000);
  }

  SUBCASE("100 events are sent") {
    for (int i = 0; i < 100; i++) {
      cly::Event event("click", i);
      countly.addEvent(event);
    }

    countly.updateSession();

    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall();
    CHECK(http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);

    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    CHECK(events.size() == 100);
  }

  SUBCASE("event with single quotes in its key and segmentation is sent unchanged") {
    cly::Event event("Player's profile", 1);
    event.addSegmentation("name", "O'Brien");
    event.addSegmentation("note", "it''s");
    countly.addEvent(event);
    REQUIRE(countly.checkEQSize() == 1);

    countly.updateSession();
    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall();

    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "Player's profile");
    CHECK(events[0]["segmentation"]["name"].get<std::string>() == "O'Brien");
    CHECK(events[0]["segmentation"]["note"].get<std::string>() == "it''s");
  }
}

/**
 * endSession() used to read began_session before taking the instance mutex and
 * clear it after, so two concurrent calls could both see an active session and
 * both queue an end_session request. stop() on one thread while another changes
 * the device id is enough to get there.
 */
TEST_CASE("concurrent endSession calls end the session only once") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  // A client that never succeeds, so the update loop cannot remove the queued
  // requests this test is counting.
  countly.setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
    (void)use_post;
    (void)url;
    (void)data;
    HTTPResponse response{false, nlohmann::json::object()};
    return response;
  });
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  const int rq_before = countly.checkRQSize(); // holds the begin_session request
  REQUIRE(rq_before >= 0);

  std::vector<std::thread> workers;
  std::atomic<int> ended(0);
  for (int t = 0; t < 4; t++) {
    workers.emplace_back([&countly, &ended]() {
      if (countly.endSession()) {
        ended.fetch_add(1);
      }
    });
  }
  for (std::thread &worker : workers) {
    worker.join();
  }

  // All four calls report success (three of them because there was nothing left
  // to end), but exactly one end_session request may be queued.
  CHECK(ended.load() == 4);
  CHECK(countly.checkRQSize() == rq_before + 1);
}

/**
 * Returns the captured requests sent for the given device ID that carry the given parameter.
 */
static std::vector<HTTPCall> callsFor(const std::string &device_id, const std::string &parameter) {
  std::vector<HTTPCall> calls;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    std::map<std::string, std::string>::const_iterator device = call.data.find("device_id");
    if (device != call.data.end() && device->second == device_id && call.data.find(parameter) != call.data.end()) {
      calls.push_back(call);
    }
  }
  return calls;
}

/**
 * User details set after start() used to stay stored for the next session, so after a
 * device ID change without merge the new user's first session carried the previous
 * user's details.
 */
TEST_CASE("user details are sent only for the device ID they were set for") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  const std::string second_device_id = "second-user-device";

  SUBCASE("details set after start are not sent with the next user's session") {
    test_utils::initCountlyWithFakeNetworking(true, countly);

    countly.setUserDetails({{"name", "First User"}});
    countly.setCustomUserDetails({{"plan", "gold"}});
    countly.setDeviceID(second_device_id, false);
    countly.processRQDebug();

    std::vector<HTTPCall> first_user_details = callsFor(COUNTLY_TEST_DEVICE_ID, "user_details");
    REQUIRE(first_user_details.size() == 2);
    CHECK(nlohmann::json::parse(first_user_details[0].data["user_details"]) == nlohmann::json({{"name", "First User"}}));
    CHECK(nlohmann::json::parse(first_user_details[1].data["user_details"]) == nlohmann::json({{"custom", {{"plan", "gold"}}}}));

    CHECK(callsFor(second_device_id, "begin_session").size() == 1);
    CHECK(callsFor(second_device_id, "user_details").empty());
  }

  SUBCASE("details set after start are not sent with a session the app begins for the next user") {
    countly.enableManualSessionControl();
    test_utils::initCountlyWithFakeNetworking(true, countly);

    countly.setUserDetails({{"name", "First User"}});
    countly.setDeviceID(second_device_id, false);
    countly.beginSession();
    countly.processRQDebug();

    CHECK(callsFor(COUNTLY_TEST_DEVICE_ID, "user_details").size() == 1);
    CHECK(callsFor(second_device_id, "begin_session").size() == 1);
    CHECK(callsFor(second_device_id, "user_details").empty());
  }

  SUBCASE("details set before start are sent with the first session only") {
    countly.setUserDetails({{"name", "First User"}});
    test_utils::initCountlyWithFakeNetworking(false, countly);

    countly.setDeviceID(second_device_id, false);
    countly.processRQDebug();

    std::vector<HTTPCall> first_user_sessions = callsFor(COUNTLY_TEST_DEVICE_ID, "begin_session");
    REQUIRE(first_user_sessions.size() == 1);
    REQUIRE(first_user_sessions[0].data.find("user_details") != first_user_sessions[0].data.end());
    CHECK(nlohmann::json::parse(first_user_sessions[0].data["user_details"]) == nlohmann::json({{"name", "First User"}}));

    CHECK(callsFor(second_device_id, "begin_session").size() == 1);
    CHECK(callsFor(second_device_id, "user_details").empty());
  }

  SUBCASE("details still waiting for the first session are sent for the first user when the user changes") {
    countly.enableManualSessionControl();
    countly.setUserDetails({{"name", "First User"}});
    test_utils::initCountlyWithFakeNetworking(true, countly);

    countly.setDeviceID(second_device_id, false);
    countly.beginSession();
    countly.processRQDebug();

    std::vector<HTTPCall> first_user_details = callsFor(COUNTLY_TEST_DEVICE_ID, "user_details");
    REQUIRE(first_user_details.size() == 1);
    CHECK(nlohmann::json::parse(first_user_details[0].data["user_details"]) == nlohmann::json({{"name", "First User"}}));

    CHECK(callsFor(second_device_id, "begin_session").size() == 1);
    CHECK(callsFor(second_device_id, "user_details").empty());
  }

  SUBCASE("details still waiting for the first session go with it after a device ID change with merge") {
    countly.enableManualSessionControl();
    countly.setUserDetails({{"name", "First User"}});
    test_utils::initCountlyWithFakeNetworking(true, countly);

    countly.setDeviceID(second_device_id, true);
    countly.beginSession();
    countly.processRQDebug();

    CHECK(callsFor(COUNTLY_TEST_DEVICE_ID, "user_details").empty());
    std::vector<HTTPCall> merged_user_sessions = callsFor(second_device_id, "begin_session");
    REQUIRE(merged_user_sessions.size() == 1);
    REQUIRE(merged_user_sessions[0].data.find("user_details") != merged_user_sessions[0].data.end());
    CHECK(nlohmann::json::parse(merged_user_sessions[0].data["user_details"]) == nlohmann::json({{"name", "First User"}}));
  }
}
