#ifndef COUNTLY_TEST_UTILS_HPP_
#define COUNTLY_TEST_UTILS_HPP_

#include "countly.hpp"
#include "doctest.h"
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

/**
 * Generates click events for the given countly instance
 */
static void generateEvents(int events, cly::Countly &countly) {
  for (int i = 0; i < events; i++) {
    cly::Event event("click", i); // TODO: the key name should be settable too
    countly.addEvent(event);
  }
}

/*
 * Checks the RQ, takes the top request and checks the 'events' key's value (which is an array of events) and returns the amount of events in that array
 */
static void checkTopRequestEventSize(int size, cly::Countly &countly) {
  // process the RQ so that the events are sent to the local HTTP request queue
  countly.processRQDebug();

  // check that the local HTTP request queue has atleast 1 event
  CHECK(!http_call_queue.empty());
  // get the oldest event
  HTTPCall oldest_call = http_call_queue.front();
  // remove the oldest event from the queue
  http_call_queue.pop_front();
  HTTPCall http_call = oldest_call;

  // check that the events are in the request
  nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
  CHECK(events.size() == size);
}

static void storageModuleNotInitialized(std::shared_ptr<StorageModuleBase> storageModule) {
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQInsertAtEnd("request");
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  storageModule->RQClearAll();
  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);

  std::shared_ptr<DataEntry> entry = storageModule->RQPeekFront();
  CHECK(entry->getId() == -1);
  CHECK(entry->getData() == "");

  CHECK(storageModule->RQCount() == -1);
  CHECK(storageModule->RQPeekAll().size() == 0);
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
  } else if ((http_call.url == "/o/sdk") && (http_call.data["method"] == "fetch_remote_config")) {
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

static void initCountlyWithFakeNetworking(bool clearInitialNetworkingState, cly::Countly &countly) {
  // set the HTTP client to the fake one which just stores the HTTP calls in a queue
  countly.setHTTPClient(fakeSendHTTP);

  // set the device ID to the test device ID and path to the test database
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  CHECK(countly.checkEQSize() == -1);

  // start the Countly SDK
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  CHECK(countly.checkEQSize() == 0);

  // Process the RQ so that thing will be at the http call queue
  countly.processRQDebug();
  if (clearInitialNetworkingState) {
    countly.clearRequestQueue(); // request queue contains session begin request
    http_call_queue.clear();     // cl+ear local HTTP request queue.
  }
}
} // namespace test_utils

#endif
