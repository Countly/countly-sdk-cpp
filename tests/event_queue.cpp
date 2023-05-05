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
    CHECK(countly.checkPersistentEQSize() == 20);

    // RQ should have the 100 events
    test_utils::checkTopRequestEventSize(100, countly);
  }

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
    test_utils::checkTopRequestEventSize(100, countly);
  }
}

TEST_CASE("Tests that use a custom value of event queue threshold") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  SUBCASE("Custom threshold size should be used instead of the default one") {
    countly.setEventsToRQThreshold(90); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);

    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkTopRequestEventSize(90, countly);
  }

  // Setting the threshold size after start
  SUBCASE("Custom threshold size should be used instead of the default one") {
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(90); // after start

    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkTopRequestEventSize(90, countly);
  }

  // Setting a negative threshold size
  SUBCASE("UINT_MAX should be used instead of the default one") {
    countly.setEventsToRQThreshold(-6); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    // generate 10000 events
    test_utils::generateEvents(1, countly);

    // threshold is now set to UINT_MAX (as we use unsigned int) so we should have all events still in the EQ
    CHECK(countly.checkPersistentEQSize() == 0);

    test_utils::checkTopRequestEventSize(1, countly);
  }

  // Setting threshold size both before and after start
  SUBCASE("custom threshold should be used instead of default threshold") {
    countly.setEventsToRQThreshold(-5); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(90); // after start

    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkTopRequestEventSize(90, countly);
  }

  // Setting threshold size both before and after start, with update session in between
  SUBCASE("custom threshold should be used instead of default threshold") {
    countly.setEventsToRQThreshold(-5); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.updateSession();
    countly.setEventsToRQThreshold(90); // after start

    // generate 100 events
    test_utils::generateEvents(100, countly);

    // new threshold size is 90 so we should have 10 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 10);

    // RQ should have the 90 events
    test_utils::checkTopRequestEventSize(90, countly);
  }

  // Setting threshold size both before and after start, non-merge device ID change
  SUBCASE("custom threshold should be used instead of default threshold") {
    countly.setEventsToRQThreshold(-2); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);
    countly.setEventsToRQThreshold(3); // after start
    countly.setDeviceID("new-device-id", false);

    // generate 4 events
    test_utils::generateEvents(4, countly);

    // new threshold size is 3 so we should have 1 events in the EQ left
    CHECK(countly.checkPersistentEQSize() == 1);

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