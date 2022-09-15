#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <chrono>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "countly.hpp"
#include "doctest.h"

#include "nlohmann/json.hpp"
using json = nlohmann::json;
using namespace cly;
using namespace std::literals::chrono_literals;

#define COUNTLY_TEST_APP_KEY "a32cb06789a6e99958d628378ee66bf8583a454f"
#define COUNTLY_TEST_DEVICE_ID "11732aa3-19a6-4272-9057-e3411f1938be"
#define COUNTLY_TEST_HOST "http://test.countly.notarealdomain"
#define COUNTLY_TEST_PORT 8080

#ifdef COUNTLY_USE_SQLITE
#define WAIT_FOR_SQLITE(N) std::this_thread::sleep_for(std::chrono::seconds(N))
#else
#define WAIT_FOR_SQLITE(N)
#endif

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

Countly::HTTPResponse fakeSendHTTP(bool use_post, const std::string &url, const std::string &data) {
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

  Countly::HTTPResponse response{false, json::object()};

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

void logToConsole(Countly::LogLevel level, const std::string &message) { std::cout << level << '\t' << message << std::endl; }

long long getUnixTimestamp() {
  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  return timestamp.count();
}

HTTPCall popHTTPCall() {
  std::this_thread::sleep_for(5s);

  CHECK(!http_call_queue.empty());
  HTTPCall oldest_call = http_call_queue.front();
  http_call_queue.pop_front();
  return oldest_call;
}

TEST_CASE("urlencoding is correct") {
  CHECK(Countly::encodeURL("hello world") == "hello%20world");
  CHECK(Countly::encodeURL("hello.~world") == "hello.~world");
  CHECK(Countly::encodeURL("{\"key\":\"win\",\"count\":3}") == "%7B%22key%22%3A%22win%22%2C%22count%22%3A3%7D");
  CHECK(Countly::encodeURL("测试") == "%E6%B5%8B%E8%AF%95");
}

#ifdef COUNTLY_USE_CUSTOM_SHA256
std::string customChecksumCalculator(const std::string &data) {
  std::string result = data.c_str();
  result.append("-custom_sha");
  return result;
}

void printLog(Countly::LogLevel level, const std::string &msg) {
  CHECK(msg == "message");
  CHECK(level == Countly::LogLevel::DEBUG);
}

TEST_CASE("Logger function validation") {
  Countly &countly = Countly::getInstance();

  CHECK(countly.getLogger() == nullptr);
  countly.setLogger(printLog);
  CHECK(countly.getLogger() != nullptr);

  countly.getLogger()(Countly::LogLevel::DEBUG, "message");

  countly.setLogger(nullptr);
  CHECK(countly.getLogger() == nullptr);
}

TEST_CASE("custom sha256 function validation") {
  Countly &countly = Countly::getInstance();

  std::string salt = "test-salt";
  std::string checksum = countly.calculateChecksum(salt, "hello world:");
  CHECK(checksum == ""); // when customSha256 isn't set.

  countly.setSha256(customChecksumCalculator);
  salt = "test-salt";
  checksum = countly.calculateChecksum(salt, "hello world:");
  CHECK(checksum == "hello world:test-salt-custom_sha");

  salt = "š ūļ ķ";
  checksum = countly.calculateChecksum(salt, "测试:");
  CHECK(checksum == "测试:š ūļ ķ-custom_sha");
}
#else
TEST_CASE("checksum function validation") {
  Countly &countly = Countly::getInstance();
  std::string salt = "test-salt";
  std::string checksum = countly.calculateChecksum(salt, "hello world");
  CHECK(checksum == "aaf992c81357b0ed1bb404826e01825568126ebeb004c3bc690d3d8e0766a3cc");

  salt = "š ūļ ķ";
  checksum = countly.calculateChecksum(salt, "测试");
  CHECK(checksum == "f51d24b0cb938e2f40b1f8609c62bf2508e24bcaa3b6b1a7fbf108d3c7f2f073");
}
#endif

TEST_CASE("forms are serialized correctly") {
  CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key1", "value1"}, {"key2", "value2"}})) == "key1=value1&key2=value2");
  CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key", "hello world"}})) == "key=hello%20world");
}

TEST_CASE("events are sent correctly") {
  Countly &countly = Countly::getInstance();

  countly.setLogger(logToConsole);
  countly.setHTTPClient(fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.enableRemoteConfig();

#ifdef COUNTLY_USE_SQLITE
  countly.setDatabasePath("countly-test.db");
#endif

  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  long long timestamp;

  SUBCASE("session begins") {
    countly.beginSession();

    SUBCASE("remote config is fetched") {
      HTTPCall http_call = popHTTPCall();
      CHECK(!http_call.use_post);
      CHECK(http_call.url == "/o/sdk");
      CHECK(http_call.data["method"] == "fetch_remote_config");
    }

    HTTPCall http_call = popHTTPCall();
    timestamp = getUnixTimestamp();
    long long timestampDiff = timestamp - stoll(http_call.data["timestamp"]);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(http_call.data["begin_session"] == "1");
    CHECK(timestampDiff >= 0);
  }

  SUBCASE("single event is sent") {
    cly::Event event("win", 4);
    event.addSegmentation("points", 100);
    countly.addEvent(event);
    WAIT_FOR_SQLITE(1);
    countly.updateSession();

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
    WAIT_FOR_SQLITE(1);
    countly.updateSession();

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

    WAIT_FOR_SQLITE(1);
    countly.updateSession();

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
    WAIT_FOR_SQLITE(1);
    countly.updateSession();

    HTTPCall http_call = popHTTPCall();
    CHECK(http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
  }

  timestamp = getUnixTimestamp();
  SUBCASE("session ends") {
    countly.stop();
    std::vector<std::string> requests = countly.debugReturnStateOfRQ();
    std::string data = requests.at(requests.size() - 1);
    fakeSendHTTP(false, "", data);
    HTTPCall http_call = popHTTPCall();
    long long timestampDiff = stoll(http_call.data["timestamp"]) - timestamp;
    CHECK(!http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(http_call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(http_call.data["end_session"] == "1");
    CHECK(timestampDiff >= 0);
  }
}
