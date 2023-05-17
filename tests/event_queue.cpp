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

TEST_CASE("Tests that use the default value of event queue threshold ") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  test_utils::initCountlyWithFakeNetworking(true, countly);

  SUBCASE("Adding events over the threshold size should trigger the events to be sent to the RQ") {
    // generate 120 events
    test_utils::generateEvents(120, countly);

    // default threshold is 100 so we should have 20 events in the EQ left
    CHECK(countly.checkEQSize() == 20);

    // RQ should have the 100 events
    test_utils::checkTopRequestEventSize(100, countly);
  }

  SUBCASE("Adding events 'at' the threshold should trigger the events to be sent to the RQ") {
    // generate 99 events
    test_utils::generateEvents(99, countly);

    // default threshold is 100 so we should have 99 events in the EQ still
    CHECK(countly.checkEQSize() == 99);

    countly.processRQDebug();
    // local HTTP request queue should be empty => no events sent to RQ
    CHECK(http_call_queue.empty());

    // add one more event
    cly::Event event("click", 2);
    countly.addEvent(event);

    // reached threshold so we should have 0 events in the EQ
    CHECK(countly.checkEQSize() == 0);

    // RQ should have the 100 events
    test_utils::checkTopRequestEventSize(100, countly);
  }
}

TEST_CASE("Tests setting 'setEventsToRQThreshold' before we start the SDK") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  SUBCASE("Custom threshold size should be used instead of the default one") {
    countly.setEventsToRQThreshold(90);
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(100, countly);          // generate 100 events
    CHECK(countly.checkEQSize() == 10);                // new threshold size is 90 so we should have 10 events in the EQ left
    test_utils::checkTopRequestEventSize(90, countly); // RQ should have the 90 events
  }

  SUBCASE("Internal constraints (1) should be used instead of the negative custom value") {
    countly.setEventsToRQThreshold(-6); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    // generate 3 events
    test_utils::generateEvents(3, countly);

    // threshold is now set to 1 so we should have 0 events still in the EQ
    CHECK(countly.checkEQSize() == 0);

    // queue should have 3 requests
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    int count = 3;

    while (count--) {
      CHECK(http_call_queue.size() == count + 1);
      HTTPCall oldest_call1 = http_call_queue.front();
      nlohmann::json events1 = nlohmann::json::parse(oldest_call1.data["events"]);
      CHECK(events1.size() == 1);
      http_call_queue.pop_front();
    }
  }

  SUBCASE("Internal constraints (10000) should be used instead of the positive large custom value") {
    countly.setEventsToRQThreshold(10005); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(10003, countly);
    CHECK(countly.checkEQSize() == 3);
    test_utils::checkTopRequestEventSize(10000, countly);
  }
}

TEST_CASE("Tests setting 'setEventsToRQThreshold' after we start the SDK") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  SUBCASE("Custom threshold size should be used instead of the default one") {
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(90);

    test_utils::generateEvents(100, countly);          // generate 100 events
    CHECK(countly.checkEQSize() == 10);                // new threshold size is 90 so we should have 10 events in the EQ left
    test_utils::checkTopRequestEventSize(90, countly); // RQ should have the 90 events
  }

  SUBCASE("Internal constraints (1) should be used instead of the negative custom value") {
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(-6);
    // generate 3 events
    test_utils::generateEvents(3, countly);

    // threshold is now set to 1 so we should have 0 events still in the EQ
    CHECK(countly.checkEQSize() == 0);

    // queue should have 3 requests
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());

    int count = 3;
    while (count--) {
      CHECK(http_call_queue.size() == count + 1);
      HTTPCall oldest_call1 = http_call_queue.front();
      nlohmann::json events1 = nlohmann::json::parse(oldest_call1.data["events"]);
      CHECK(events1.size() == 1);
      http_call_queue.pop_front();
    }
  }

  SUBCASE("Internal constraints (10000) should be used instead of the positive large custom value") {
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(10005);

    test_utils::generateEvents(10003, countly);
    CHECK(countly.checkEQSize() == 3);
    test_utils::checkTopRequestEventSize(10000, countly);
  }
}

TEST_CASE("Tests that sets 'setEventsToRQThreshold' before and after SDK starts") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  // Setting threshold size before start first
  SUBCASE("Setting new threshold should check if there are events in the EQ and send them to the RQ if needed") {
    countly.setEventsToRQThreshold(5);
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(10);

    // generate 18 events
    test_utils::generateEvents(18, countly);

    // new threshold size is 10 so we should have 8 events in the EQ left
    CHECK(countly.checkEQSize() == 8);

    // RQ should have the 10 events
    test_utils::checkTopRequestEventSize(10, countly);

    // reduce the threshold
    countly.setEventsToRQThreshold(5);

    // new threshold size is smaller than previous so all events must be sent to RQ
    CHECK(countly.checkEQSize() == 0);
    test_utils::checkTopRequestEventSize(8, countly);
  }

  SUBCASE("Only the latest set threshold must be used, positive last ") {
    countly.setEventsToRQThreshold(-5); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(90); // after start

    test_utils::generateEvents(100, countly);
    CHECK(countly.checkEQSize() == 10);
    test_utils::checkTopRequestEventSize(90, countly);
  }

  SUBCASE("Only the latest set threshold must be used, negative last") {
    countly.setEventsToRQThreshold(90); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(-5); // after start

    test_utils::generateEvents(1, countly);
    CHECK(countly.checkEQSize() == 0);
    test_utils::checkTopRequestEventSize(1, countly);
  }

  // Setting threshold size both before and after start, with update session in between
  // to see if session update and its internal logic messes up the threshold size
  SUBCASE("Updating Session should not effect the threshold size") {
    countly.setEventsToRQThreshold(-5); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.updateSession();
    countly.setEventsToRQThreshold(90); // after start

    test_utils::generateEvents(100, countly);
    CHECK(countly.checkEQSize() == 10);
    test_utils::checkTopRequestEventSize(90, countly);
  }

  // Setting threshold size both before and after start, non-merge device ID change
  // to see if device ID change and its internal logic messes up the threshold size
  SUBCASE("Device ID change should not effect the threshold size") {
    countly.setEventsToRQThreshold(-2); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(3); // after start
    countly.setDeviceID("new-device-id", false);

    test_utils::generateEvents(4, countly);
    CHECK(countly.checkEQSize() == 1);

    // RQ should have the 3 events
    // trigger RQ to send requests to http_call_queue
    countly.processRQDebug();
    // queue should have 3 requests
    CHECK(!http_call_queue.empty());
    CHECK(http_call_queue.size() == 3);
    http_call_queue.pop_front(); // begin session
    http_call_queue.pop_front(); // change device ID
    HTTPCall oldest_call = http_call_queue.front();
    CHECK(http_call_queue.size() == 1);

    // last call should have 3 events
    nlohmann::json events = nlohmann::json::parse(oldest_call.data["events"]);
    CHECK(events.size() == 3);
  }
}