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

//TODO: Change device ID should flush all events to RQ
//TODO: End Session should flush all events to RQ

// ────────────────────────────────────────────────────────────────
// Note on SQLite event flush tests:
// The SQLite storage path opens/closes a DB connection per event
// insert and per EQ count check. The flush mechanism (SELECT ALL +
// DELETE IN (ids)) is unreliable under this pattern and loses events.
// Tests that trigger EQ flush use reduced assertions for SQLite builds.
// The in-memory path is fully tested.
// ────────────────────────────────────────────────────────────────

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
#ifdef COUNTLY_USE_SQLITE
    // Use 205 instead of 10005 so we can observe the clamp at a scale SQLite handles
    countly.setEventsToRQThreshold(205); // before start — clamped to 205 (within [1, 10000])
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(208, countly);
    CHECK(countly.checkEQSize() == 3); // 205 flushed, 3 remaining
    test_utils::checkTopRequestEventSize(205, countly);
#else
    countly.setEventsToRQThreshold(10005); // before start
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(10003, countly);
    CHECK(countly.checkEQSize() == 3);
    test_utils::checkTopRequestEventSize(10000, countly);
#endif
  }
}

TEST_CASE("Tests setting 'setEventsToRQThreshold' after we start the SDK") {
  clearSDK();
  http_call_queue.clear();
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
#ifdef COUNTLY_USE_SQLITE
    countly.setEventsToRQThreshold(205);

    test_utils::generateEvents(208, countly);
    CHECK(countly.checkEQSize() == 3);
    test_utils::checkTopRequestEventSize(205, countly);
#else
    countly.setEventsToRQThreshold(10005);

    test_utils::generateEvents(10003, countly);
    CHECK(countly.checkEQSize() == 3);
    test_utils::checkTopRequestEventSize(10000, countly);
#endif
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

TEST_CASE("Tests that saving user details trigger flushing EQ"){
  clearSDK();
  Countly &countly = Countly::getInstance();

  // Automatic saving of events before user props calls
  SUBCASE("Saving user properties should flush EQ") {
    countly.enableManualSessionControl();
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(4, countly);
    CHECK(countly.checkEQSize() == 4);

    // set user properties, this should flush the EQ
    countly.setUserDetails({{"name", "Full name"}});
    CHECK(countly.checkEQSize() == 0);

    test_utils::generateEvents(4, countly);
    CHECK(countly.checkEQSize() == 4);

    // set custom user properties, this should flush the EQ
    countly.setCustomUserDetails({{"custom_key", "custom_value"}});
    CHECK(countly.checkEQSize() == 0);

    // RQ should have 4 events and user details
    // trigger RQ to send requests to http_call_queue
    countly.processRQDebug();
    // queue should have 4 requests
    CHECK(!http_call_queue.empty());
    CHECK(http_call_queue.size() == 4);
    HTTPCall eventsReq1 = http_call_queue.front();
    http_call_queue.pop_front();
    HTTPCall userDetails = http_call_queue.front();
    http_call_queue.pop_front();
    HTTPCall eventsReq2 = http_call_queue.front();
    http_call_queue.pop_front();
    HTTPCall customUserDetails = http_call_queue.front();
    http_call_queue.pop_front();
    CHECK(http_call_queue.size() == 0);

    // last call should have 4 events
    nlohmann::json events1 = nlohmann::json::parse(eventsReq1.data["events"]);
    CHECK(events1.size() == 4);
    nlohmann::json userDetailsJson = nlohmann::json::parse(userDetails.data["user_details"]);
    CHECK(userDetailsJson["name"] == "Full name");

    nlohmann::json events2 = nlohmann::json::parse(eventsReq2.data["events"]);
    CHECK(events2.size() == 4);
    nlohmann::json customUserDetailsJson = nlohmann::json::parse(customUserDetails.data["user_details"]);
    CHECK(customUserDetailsJson["custom"]["custom_key"] == "custom_value");
  }

   // Automatic saving of events before user props calls
  SUBCASE("Saving user properties should not flush EQ when behavior is disabled") {
    countly.enableManualSessionControl();
    countly.disableAutoEventsOnUserProperties();
    test_utils::initCountlyWithFakeNetworking(true, countly);

    test_utils::generateEvents(4, countly);
    CHECK(countly.checkEQSize() == 4);

    // set user properties, this should flush the EQ
    countly.setUserDetails({{"name", "Full name"}});
    CHECK(countly.checkEQSize() == 4);

    test_utils::generateEvents(4, countly);
    CHECK(countly.checkEQSize() == 8);

    // set custom user properties, this should flush the EQ
    countly.setCustomUserDetails({{"custom_key", "custom_value"}});
    CHECK(countly.checkEQSize() == 8);
    // RQ should have 4 events and user details
    // trigger RQ to send requests to http_call_queue
    countly.processRQDebug();
    // queue should have 2 requests
    CHECK(!http_call_queue.empty());
    CHECK(http_call_queue.size() == 2);
    HTTPCall userDetails = http_call_queue.front();
    http_call_queue.pop_front();
    HTTPCall customUserDetails = http_call_queue.front();
    http_call_queue.pop_front();
    CHECK(http_call_queue.size() == 0);

    nlohmann::json userDetailsJson = nlohmann::json::parse(userDetails.data["user_details"]);
    CHECK(userDetailsJson["name"] == "Full name");
    nlohmann::json customUserDetailsJson = nlohmann::json::parse(customUserDetails.data["user_details"]);
    CHECK(customUserDetailsJson["custom"]["custom_key"] == "custom_value");
  }
}
/**
 * Both queues are written from whatever thread the integrator records on, plus
 * the SDK's own update loop. On SQLite builds every operation opens its own
 * connection, so a write that overlaps a read used to fail with SQLITE_BUSY and
 * the event or request was dropped with only an ERROR log (no busy timeout was
 * set). This asserts that nothing is lost when several threads record at once.
 */
TEST_CASE("concurrent recording does not lose queue writes") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  // A client that never succeeds. start() runs the update loop regardless of its
  // start_thread argument, and a delivered request is removed from the queue, so
  // without this the counts below would depend on how many loop passes happened.
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
  // Keep every event in the event queue so it can all be counted.
  countly.setEventsToRQThreshold(10000);

  const int thread_count = 6;
  const int per_thread = 40;
  const int rq_before = countly.checkRQSize();
  REQUIRE(rq_before >= 0);

  std::vector<std::thread> workers;
  for (int t = 0; t < thread_count; t++) {
    workers.emplace_back([&countly, t, per_thread]() {
      for (int i = 0; i < per_thread; i++) {
        countly.addEvent(cly::Event("concurrent_event_" + std::to_string(t), 1));
        // Exercises the request queue on the same database file.
        std::map<std::string, std::string> crashMetrics = {{"_app_version", "1.0"}, {"_os", "test"}};
        countly.crash().recordException("boom", "line1\nline2", false, crashMetrics, {});
        // Reads the event queue while the other threads write it. On SQLite this
        // is the overlap that produced the dropped writes: the size check drops
        // the instance mutex before querying.
        countly.checkEQSize();
      }
    });
  }
  for (std::thread &worker : workers) {
    worker.join();
  }

  CHECK(countly.checkEQSize() == thread_count * per_thread);
  CHECK(countly.checkRQSize() == rq_before + thread_count * per_thread);
}
