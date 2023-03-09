#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>

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

  SUBCASE("init sdk - session begin ") {
    countly.processRQDebug();
    HTTPCall http_call = popHTTPCall();
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
}
