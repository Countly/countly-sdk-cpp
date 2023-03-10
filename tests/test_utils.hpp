#ifndef COUNTLY_TEST_UTILS_HPP_
#define COUNTLY_TEST_UTILS_HPP_

#include "countly.hpp"
#include "nlohmann/json.hpp"
#include <cstdio>

using namespace cly;

namespace test_utils {
#define COUNTLY_TEST_APP_KEY "a32cb06789a6e99958d628378ee66bf8583a454f"
#define COUNTLY_TEST_DEVICE_ID "11732aa3-19a6-4272-9057-e3411f1938be"
#define COUNTLY_TEST_HOST "http://test.countly.notarealdomain"
#define COUNTLY_TEST_PORT 8080
#define TEST_DATABASE_NAME "test-countly.db"

struct HTTPCall {
  bool use_post;
  std::string url;
  std::map<std::string, std::string> data;
};

static std::deque<HTTPCall> http_call_queue;

static void clearSDK() {
  cly::Countly::halt();
  remove(TEST_DATABASE_NAME);
}

static void decodeURL(std::string &encoded) {
  for (auto percent_index = encoded.find('%'); percent_index != std::string::npos; percent_index = encoded.find('%', percent_index + 1)) {
    std::string hex_string = encoded.substr(percent_index + 1, 2);
    char value = std::strtol(hex_string.c_str(), nullptr, 16);
    encoded.replace(percent_index, 3, std::string(1, value));
  }
}

static long long getUnixTimestamp() {
  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  return timestamp.count();
}

static HTTPResponse fakeSendHTTP(bool use_post, const std::string &url, const std::string &data) {
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

  HTTPResponse response{false, nlohmann::json::object()};

  if (http_call.url == "/i") {
    response.success = true;
  } else if (http_call.url == "/o/sdk" && http_call.data["method"] == "fetch_remote_config") {
    nlohmann::json remote_config = {{"color", "#FF9900"}, {"playerQueueTimeout", 32}, {"isChristmas", true}};

    response.success = true;

    if (http_call.data.find("keys") != http_call.data.end()) {
      auto keys = nlohmann::json::parse(http_call.data["keys"]);

      if (keys.is_array() && keys.size() > 0) {
        for (const auto &key : keys.get<std::vector<std::string>>()) {
          if (remote_config.find(key) != remote_config.end()) {
            response.data[key] = remote_config.at(key);
          }
        }
      }
    } else if (http_call.data.find("keys") != http_call.data.end()) {
      nlohmann::json omit_keys = nlohmann::json::parse(http_call.data["omit_keys"]);

      for (const auto &element : remote_config.items()) {
        if (omit_keys.find(element.key()) == omit_keys.end()) {
          response.data[element.key()] = element.value();
        }
      }
    }
  }

  return response;
}
} // namespace test_utils

#endif
