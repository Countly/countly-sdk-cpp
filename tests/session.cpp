#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>

#include "countly.hpp"
#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"
using json = nlohmann::json;
using namespace cly;
using namespace std::literals::chrono_literals;

#define COUNTLY_TEST_APP_KEY "a32cb06789a6e99958d628378ee66bf8583a454f"
#define COUNTLY_TEST_DEVICE_ID "11732aa3-19a6-4272-9057-e3411f1938be"
#define COUNTLY_TEST_HOST "http://test.countly.notarealdomain"
#define COUNTLY_TEST_PORT 8080

void decodeURL(std::string &encoded) {
  for (auto percent_index = encoded.find('%'); percent_index != std::string::npos; percent_index = encoded.find('%', percent_index + 1)) {
    std::string hex_string = encoded.substr(percent_index + 1, 2);
    char value = std::strtol(hex_string.c_str(), nullptr, 16);
    encoded.replace(percent_index, 3, std::string(1, value));
  }
}

struct HTTPCall {
  bool use_post;
  std::string url;
  std::map<std::string, std::string> data;
};

std::deque<HTTPCall> http_call_queue;

HTTPResponse fakeSendHTTP(bool use_post, const std::string &url, const std::string &data) {
  HTTPCall http_call({use_post, url, {}});

  std::string::size_type startIndex = 0;
  for (auto seperatorIndex = data.find('&'); seperatorIndex != std::string::npos;) {
    auto assignmentIndex = data.find('=', startIndex);
    if (assignmentIndex == std::string::npos || assignmentIndex >= seperatorIndex) {
      http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = "";
    } else {
      http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = data.substr(assignmentIndex + 1, seperatorIndex - assignmentIndex - (seperatorIndex == (data.length() - 1) ? 0 : 1));
      decodeURL(http_call.data[data.substr(startIndex, assignmentIndex - startIndex)]);
    }

    if (seperatorIndex != (data.length() - 1)) {
      startIndex = seperatorIndex + 1;
      seperatorIndex = data.find('&', startIndex);

      if (seperatorIndex == std::string::npos) {
        seperatorIndex = data.length() - 1;
      }
    } else {
      seperatorIndex = std::string::npos;
    }
  }

  http_call_queue.push_back(http_call);

  HTTPResponse response{false, json::object()};

  if (http_call.url == "/i") {
    response.success = true;
  } else if (http_call.url == "/o/sdk" && http_call.data["method"] == "fetch_remote_config") {
    const json remote_config = {{"color", "#FF9900"}, {"playerQueueTimeout", 32}, {"isChristmas", true}};

    response.success = true;

    if (http_call.data.find("keys") != http_call.data.end()) {
      auto keys = json::parse(http_call.data["keys"]);

      if (keys.is_array() && keys.size() > 0) {
        for (const auto &key : keys.get<std::vector<std::string>>()) {
          if (remote_config.find(key) != remote_config.end()) {
            response.data[key] = remote_config.at(key);
          }
        }
      }
    } else if (http_call.data.find("keys") != http_call.data.end()) {
      json omit_keys = json::parse(http_call.data["omit_keys"]);

      for (const auto &element : remote_config.items()) {
        if (omit_keys.find(element.key()) == omit_keys.end()) {
          response.data[element.key()] = element.value();
        }
      }
    }
  }

  return response;
}

long long getUnixTimestamp() {
  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  return timestamp.count();
}

HTTPCall popHTTPCall() {
  CHECK(!http_call_queue.empty());
  HTTPCall oldest_call = http_call_queue.front();
  http_call_queue.pop_front();
  return oldest_call;
}

TEST_CASE("sessions unit tests") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  countly.setHTTPClient(fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.setAutomaticSessionUpdateInterval(2);
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

  countly.setHTTPClient(fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
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
