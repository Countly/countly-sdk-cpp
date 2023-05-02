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

TEST_CASE("Default threshold") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("Adding events over the threshold size should trigger the events to be sent to the RQ") {
    // generate 120 events
		test_utils::generateEvents(120, countly);

    // default threshold is 100 so we should have 20 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 20);

		// RQ should have the 100 events
    test_utils::checkEventSizeInRQ(100, countly);
  }
}

TEST_CASE("Default threshold 2") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("Adding events 'at' the threshold should trigger the events to be sent to the RQ") {
    // generate 99 events
    test_utils::generateEvents(99, countly);

    // default threshold is 100 so we should have 99 events in the EQ still
    CHECK(countly.checkPersistentEQSize() == 99);

    countly.processRQDebug();
    // local HTTP request queue should be empty => no events sent to RQ
    CHECK(http_call_queue.empty());

		// add one more event
    cly::Event event("click", 2);
    countly.addEvent(event);

    // reached threshold so we should have 0 events in the EQ
    CHECK(countly.checkPersistentEQSize() == 0);

    // RQ should have the 100 events
    test_utils::checkEventSizeInRQ(100, countly);
  }
}

TEST_CASE("Setting the threshold size before start") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setEventSendingThreshold(90); // before start
	countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("Custom threshold size should be used instead of the default one") {
    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkEventSizeInRQ(90, countly);
  }
}

TEST_CASE("Setting the threshold size after start") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.setEventSendingThreshold(90); // after start

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("Custom threshold size should be used instead of the default one") {
    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkEventSizeInRQ(90, countly);
  }
}

TEST_CASE("Setting a negative threshold size") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setEventSendingThreshold(-6); // before start
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("UINT_MAX should be used instead of the default one") {
    // generate 10000 events
    test_utils::generateEvents(10000, countly);

    // threshold is now set to UINT_MAX (as we use unsigned int) so we should have all events still in the EQ
    CHECK(countly.checkPersistentEQSize() == 10000);

    countly.processRQDebug();
    // local HTTP request queue should be empty => no events sent to RQ
    CHECK(http_call_queue.empty());
  }
}

TEST_CASE("Setting threshold size both before and after start") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setEventSendingThreshold(-5); // before start
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.setEventSendingThreshold(90); // after start

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("custom threshold should be used instead of default threshold") {
    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkEventSizeInRQ(90, countly);
  }
}

TEST_CASE("Setting threshold size both before and after start, with update session in between") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setEventSendingThreshold(-5); // before start
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.updateSession();
  countly.setEventSendingThreshold(90); // after start

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("custom threshold should be used instead of default threshold") {
    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkEventSizeInRQ(90, countly);
  }
}

TEST_CASE("Setting threshold size both before and after start, non-merge device ID change") {
  clearSDK();

  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setEventSendingThreshold(-5); // before start
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.setEventSendingThreshold(90); // after start
  countly.setDeviceID("new-device-id", false); 

  countly.processRQDebug();
  countly.clearRequestQueue(); // request queue contains session begin request
  http_call_queue.clear();     // clear local HTTP request queue.

  SUBCASE("custom threshold should be used instead of default threshold") {
    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall oldest_call = http_call_queue.front();
    http_call_queue.pop_front();
    HTTPCall http_call = oldest_call;

    CHECK(http_call.use_post);
    CHECK(http_call.data["app_key"] == COUNTLY_TEST_APP_KEY);

    // check that the events are in the request
    nlohmann::json events = nlohmann::json::parse(http_call.data["events"]);
    CHECK(events.size() == 90);
  }
}