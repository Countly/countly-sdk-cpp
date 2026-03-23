#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <thread>

using namespace cly;
using namespace test_utils;

/**
 * Integration tests for the immediateRequestOnStop feature.
 *
 * These tests verify that the condition-variable-based update loop
 * behaves correctly end-to-end: session lifecycle, event delivery,
 * manual session control, and immediate shutdown responsiveness.
 * A separate test case verifies the fallback (sleep-based) path.
 */

// Helper: search http_call_queue for a request containing a specific key=value pair
static bool httpQueueContains(const std::string &key, const std::string &value) {
  for (const auto &call : http_call_queue) {
    auto it = call.data.find(key);
    if (it != call.data.end() && it->second == value) {
      return true;
    }
  }
  return false;
}

// Helper: search http_call_queue for a request containing a specific event key
static bool httpQueueContainsEvent(const std::string &event_key) {
  for (const auto &call : http_call_queue) {
    auto it = call.data.find("events");
    if (it != call.data.end()) {
      nlohmann::json events = nlohmann::json::parse(it->second);
      for (const auto &e : events) {
        if (e["key"].get<std::string>() == event_key) {
          return true;
        }
      }
    }
  }
  return false;
}

TEST_CASE("immediateRequestOnStop - session lifecycle through CV loop") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.enableImmediateRequestOnStop();
  ct.setAutomaticSessionUpdateInterval(1);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);
  // Wait for the loop to process the begin request
  std::this_thread::sleep_for(std::chrono::seconds(2));
  // Flush any remaining RQ items through our fakeSendHTTP
  ct.processRQDebug();

  // Verify session begin was sent
  CHECK(httpQueueContains("begin_session", "1"));

  // Now stop and verify session end
  http_call_queue.clear();
  ct.stop();
  ct.processRQDebug();

  CHECK(httpQueueContains("end_session", "1"));
}

TEST_CASE("immediateRequestOnStop - event delivery through CV loop") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.enableImmediateRequestOnStop();
  ct.setAutomaticSessionUpdateInterval(1);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);

  // Add events after the loop is running
  cly::Event event1("purchase", 1);
  ct.addEvent(event1);
  cly::Event event2("login", 1);
  ct.addEvent(event2);

  // Wait for the threaded update loop to pick up and process events
  // With 1-second interval, 3 seconds gives at least 2 full cycles
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ct.stop();

  CHECK(httpQueueContainsEvent("purchase"));
  CHECK(httpQueueContainsEvent("login"));
}

TEST_CASE("immediateRequestOnStop - stop responsiveness with long interval") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.enableImmediateRequestOnStop();
  // Use a long update interval to prove the CV wakes the thread, not the timeout
  ct.setAutomaticSessionUpdateInterval(60);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);
  // Let the thread enter wait_for with the 60-second interval
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto before = std::chrono::steady_clock::now();
  ct.stop();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - before)
                        .count();

  // Must complete well under the 60-second interval.
  // A generous 5-second threshold avoids CI flakiness while still
  // proving the CV woke the thread (60s vs <5s is unambiguous).
  CHECK(elapsed_ms < 5000);

  // Verify the session was still properly ended despite the immediate stop
  ct.processRQDebug();
  CHECK(httpQueueContains("end_session", "1"));
}

TEST_CASE("immediateRequestOnStop - manual session control through CV loop") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.enableImmediateRequestOnStop();
  ct.enableManualSessionControl();
  ct.setAutomaticSessionUpdateInterval(1);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);

  // In manual session mode, the loop calls packEvents() instead of updateSession()
  cly::Event event("manual_event", 5);
  ct.addEvent(event);

  // Wait for the thread to pack and send events
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ct.stop();
  // Flush any remaining items from the RQ
  ct.processRQDebug();

  // Events should be packed and delivered
  CHECK(httpQueueContainsEvent("manual_event"));

  // No automatic session begin should have been sent
  CHECK_FALSE(httpQueueContains("begin_session", "1"));
}

TEST_CASE("immediateRequestOnStop - fallback sleep path") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  // Do NOT enable immediateRequestOnStop -- exercises the old sleep_for path
  ct.setAutomaticSessionUpdateInterval(1);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);

  // Add an event while the loop is running
  cly::Event event("fallback_event", 3);
  ct.addEvent(event);

  // With 1-second interval, 3 seconds gives enough cycles to process
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ct.stop();
  ct.processRQDebug();

  // Verify event delivery works through the fallback path
  CHECK(httpQueueContainsEvent("fallback_event"));

  // Verify session lifecycle works through the fallback path
  CHECK(httpQueueContains("begin_session", "1"));
  CHECK(httpQueueContains("end_session", "1"));
}
