#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

/**
 * Helper to initialize the SDK with an SBS config JSON.
 * Uses manual session control to avoid automatic session begin requests.
 * Clears the HTTP call queue after setup so tests start with a clean state.
 */
static void initWithSBSConfig(const json &sbsConfig, Countly &countly) {
  std::string sbsStr = sbsConfig.dump();
  countly.setSDKBehaviorSettings(sbsStr);
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableManualSessionControl();
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  // Wait briefly for the async SBS config fetch thread to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  countly.processRQDebug();
  countly.clearRequestQueue();
  http_call_queue.clear();
}

/**
 * Helper to initialize the SDK WITHOUT providing SBS config.
 * Uses whatever SBS is already stored in the database (or defaults).
 * Uses manual session control to avoid automatic session begin requests.
 * Clears the HTTP call queue after setup so tests start with a clean state.
 */
static void initWithoutSBSConfig(Countly &countly) {
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableManualSessionControl();
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  countly.processRQDebug();
  countly.clearRequestQueue();
  http_call_queue.clear();
}

/**
 * Helper to pop the front HTTP call from the queue.
 */
static HTTPCall popCall() {
  CHECK(!http_call_queue.empty());
  HTTPCall call = http_call_queue.front();
  http_call_queue.pop_front();
  return call;
}

// ---------------------------------------------------------------------------
// 1. Event Filter Tests
// ---------------------------------------------------------------------------

TEST_CASE("SBS Event Filter") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Event blacklist blocks matching custom events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"eb", json::array({"blocked_event", "another_blocked"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Record a blocked event
    cly::Event blocked("blocked_event", 1);
    countly.addEvent(blocked);

    // Record an allowed event
    cly::Event allowed("allowed_event", 1);
    countly.addEvent(allowed);

    // The blocked event should have been dropped; only the allowed event should be in the EQ/RQ
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "allowed_event");
  }

  SUBCASE("Event whitelist only allows listed events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"ew", json::array({"allowed_event"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event allowed("allowed_event", 1);
    countly.addEvent(allowed);

    cly::Event notAllowed("not_allowed", 1);
    countly.addEvent(notAllowed);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "allowed_event");
  }

  SUBCASE("Empty event blacklist allows all events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"eb", json::array()}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e1("event_1", 1);
    countly.addEvent(e1);

    cly::Event e2("event_2", 1);
    countly.addEvent(e2);

    countly.processRQDebug();

    // With eqs=1, each event triggers its own RQ entry. We expect 2 requests.
    CHECK(http_call_queue.size() == 2);

    HTTPCall call1 = popCall();
    json events1 = json::parse(call1.data["events"]);
    CHECK(events1.size() == 1);
    CHECK(events1[0]["key"].get<std::string>() == "event_1");

    HTTPCall call2 = popCall();
    json events2 = json::parse(call2.data["events"]);
    CHECK(events2.size() == 1);
    CHECK(events2[0]["key"].get<std::string>() == "event_2");
  }

  SUBCASE("Internal events bypass event filter") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"eb", json::array({"[CLY]_view", "custom_blocked"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Record a view (internal event with [CLY]_ prefix)
    std::string viewId = countly.views().openView("test_view");
    CHECK(!viewId.empty());

    // Record a blocked custom event (will be dropped by filter)
    cly::Event blocked("custom_blocked", 1);
    countly.addEvent(blocked);

    // Record an allowed custom event
    cly::Event allowed("allowed_event", 1);
    countly.addEvent(allowed);

    countly.processRQDebug();

    // With eqs=1, each event that passes the filter triggers a flush to RQ.
    // View event (internal, bypasses filter) and "allowed_event" should be in RQ.
    // "custom_blocked" should have been dropped.
    int totalEvents = 0;
    bool hasViewEvent = false;
    bool hasAllowedEvent = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          totalEvents++;
          std::string key = e["key"].get<std::string>();
          if (key == "[CLY]_view") {
            hasViewEvent = true;
          }
          if (key == "allowed_event") {
            hasAllowedEvent = true;
          }
          CHECK(key != "custom_blocked");
        }
      }
    }
    CHECK(totalEvents == 2);
    CHECK(hasViewEvent);
    CHECK(hasAllowedEvent);
  }

  SUBCASE("Event blacklist takes precedence over whitelist") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    // Both blacklist and whitelist present; blacklist should take precedence
    json sbs = {{"eb", json::array({"blocked"})}, {"ew", json::array({"blocked", "other"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e1("blocked", 1);
    countly.addEvent(e1);
    cly::Event e2("other", 1);
    countly.addEvent(e2);
    cly::Event e3("third", 1);
    countly.addEvent(e3);

    countly.processRQDebug();

    // "blocked" should be dropped (in blacklist, blacklist takes precedence).
    // When blacklist is present, whitelist is ignored, so "other" and "third" should pass.
    bool hasOther = false;
    bool hasThird = false;
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          std::string key = e["key"].get<std::string>();
          CHECK(key != "blocked");
          if (key == "other") hasOther = true;
          if (key == "third") hasThird = true;
          totalEvents++;
        }
      }
    }
    CHECK(totalEvents == 2);
    CHECK(hasOther);
    CHECK(hasThird);
  }

  SUBCASE("Blacklist mode blocks listed events and allows others") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"eb", json::array({"blocked_a", "blocked_b"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e1("blocked_a", 1);
    countly.addEvent(e1);
    cly::Event e2("blocked_b", 1);
    countly.addEvent(e2);
    cly::Event e3("allowed_c", 1);
    countly.addEvent(e3);
    cly::Event e4("allowed_d", 1);
    countly.addEvent(e4);

    countly.processRQDebug();

    int totalEvents = 0;
    bool hasAllowedC = false;
    bool hasAllowedD = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          std::string key = e["key"].get<std::string>();
          CHECK(key != "blocked_a");
          CHECK(key != "blocked_b");
          if (key == "allowed_c") hasAllowedC = true;
          if (key == "allowed_d") hasAllowedD = true;
          totalEvents++;
        }
      }
    }
    CHECK(totalEvents == 2);
    CHECK(hasAllowedC);
    CHECK(hasAllowedD);
  }

  SUBCASE("Whitelist mode allows listed events and blocks others") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"ew", json::array({"allowed_a", "allowed_b"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e1("allowed_a", 1);
    countly.addEvent(e1);
    cly::Event e2("allowed_b", 1);
    countly.addEvent(e2);
    cly::Event e3("blocked_c", 1);
    countly.addEvent(e3);
    cly::Event e4("blocked_d", 1);
    countly.addEvent(e4);

    countly.processRQDebug();

    int totalEvents = 0;
    bool hasAllowedA = false;
    bool hasAllowedB = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          std::string key = e["key"].get<std::string>();
          CHECK(key != "blocked_c");
          CHECK(key != "blocked_d");
          if (key == "allowed_a") hasAllowedA = true;
          if (key == "allowed_b") hasAllowedB = true;
          totalEvents++;
        }
      }
    }
    CHECK(totalEvents == 2);
    CHECK(hasAllowedA);
    CHECK(hasAllowedB);
  }
}

// ---------------------------------------------------------------------------
// 2. Segmentation Filter Tests (global + event-specific + combined)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Segmentation Filter") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Segmentation blacklist removes matching keys") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sb", json::array({"blocked_key", "another_blocked"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event event("test_event", 1);
    event.addSegmentation("blocked_key", "v1");
    event.addSegmentation("allowed_key", "v2");
    event.addSegmentation("another_blocked", "v3");
    countly.addEvent(event);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);

    json seg = events[0]["segmentation"];
    CHECK(seg.contains("allowed_key"));
    CHECK(seg["allowed_key"].get<std::string>() == "v2");
    CHECK_FALSE(seg.contains("blocked_key"));
    CHECK_FALSE(seg.contains("another_blocked"));
  }

  SUBCASE("Segmentation whitelist keeps only listed keys") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sw", json::array({"allowed_key"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event event("test_event", 1);
    event.addSegmentation("allowed_key", "v1");
    event.addSegmentation("removed_key", "v2");
    countly.addEvent(event);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);

    json seg = events[0]["segmentation"];
    CHECK(seg.contains("allowed_key"));
    CHECK(seg["allowed_key"].get<std::string>() == "v1");
    CHECK_FALSE(seg.contains("removed_key"));
  }

  SUBCASE("Empty segmentation blacklist allows all keys") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sb", json::array()}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event event("test_event", 1);
    event.addSegmentation("key1", "v1");
    event.addSegmentation("key2", "v2");
    countly.addEvent(event);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);

    json seg = events[0]["segmentation"];
    CHECK(seg.contains("key1"));
    CHECK(seg.contains("key2"));
    CHECK(seg["key1"].get<std::string>() == "v1");
    CHECK(seg["key2"].get<std::string>() == "v2");
  }

  SUBCASE("Global and event-specific segmentation blacklists both applied") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {
        {"sb", json::array({"global_blocked"})},
        {"esb", {{"my_event", json::array({"event_blocked"})}}},
        {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event event("my_event", 1);
    event.addSegmentation("global_blocked", "v1");
    event.addSegmentation("event_blocked", "v2");
    event.addSegmentation("allowed_key", "v3");
    countly.addEvent(event);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);

    json seg = events[0]["segmentation"];
    // Both global and event-specific blocked keys should be removed
    CHECK_FALSE(seg.contains("global_blocked"));
    CHECK_FALSE(seg.contains("event_blocked"));
    CHECK(seg.contains("allowed_key"));
    CHECK(seg["allowed_key"].get<std::string>() == "v3");
  }

  SUBCASE("Event-specific filter does not affect other events") {
    // esb has rules for event1, but event2 should pass through unfiltered
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {
        {"esb", {{"event1", json::array({"secret_key"})}}},
        {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // event2 has the same key as event1's blacklist, but should not be filtered
    cly::Event e("event2", 1);
    e.addSegmentation("secret_key", "v1");
    e.addSegmentation("other_key", "v2");
    countly.addEvent(e);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "event2");

    json seg = events[0]["segmentation"];
    CHECK(seg.contains("secret_key"));
    CHECK(seg["secret_key"].get<std::string>() == "v1");
    CHECK(seg.contains("other_key"));
    CHECK(seg["other_key"].get<std::string>() == "v2");
  }

  SUBCASE("Multiple events with different per-event filters") {
    // esb has different rules per event, verify each event gets its own filter
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {
        {"esb",
         {{"eventA", json::array({"keyA"})},
          {"eventB", json::array({"keyB"})},
          {"eventC", json::array({"keyC"})}}},
        {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // eventA: keyA removed, keyB and keyC kept
    cly::Event eA("eventA", 1);
    eA.addSegmentation("keyA", "vA");
    eA.addSegmentation("keyB", "vB");
    eA.addSegmentation("keyC", "vC");
    countly.addEvent(eA);

    // eventB: keyB removed, keyA and keyC kept
    cly::Event eB("eventB", 1);
    eB.addSegmentation("keyA", "vA");
    eB.addSegmentation("keyB", "vB");
    eB.addSegmentation("keyC", "vC");
    countly.addEvent(eB);

    // eventC: keyC removed, keyA and keyB kept
    cly::Event eC("eventC", 1);
    eC.addSegmentation("keyA", "vA");
    eC.addSegmentation("keyB", "vB");
    eC.addSegmentation("keyC", "vC");
    countly.addEvent(eC);

    countly.processRQDebug();
    CHECK(http_call_queue.size() == 3);

    // eventA
    HTTPCall callA = popCall();
    json eventsA = json::parse(callA.data["events"]);
    CHECK(eventsA[0]["key"].get<std::string>() == "eventA");
    json segA = eventsA[0]["segmentation"];
    CHECK_FALSE(segA.contains("keyA"));
    CHECK(segA.contains("keyB"));
    CHECK(segA.contains("keyC"));

    // eventB
    HTTPCall callB = popCall();
    json eventsB = json::parse(callB.data["events"]);
    CHECK(eventsB[0]["key"].get<std::string>() == "eventB");
    json segB = eventsB[0]["segmentation"];
    CHECK(segB.contains("keyA"));
    CHECK_FALSE(segB.contains("keyB"));
    CHECK(segB.contains("keyC"));

    // eventC
    HTTPCall callC = popCall();
    json eventsC = json::parse(callC.data["events"]);
    CHECK(eventsC[0]["key"].get<std::string>() == "eventC");
    json segC = eventsC[0]["segmentation"];
    CHECK(segC.contains("keyA"));
    CHECK(segC.contains("keyB"));
    CHECK_FALSE(segC.contains("keyC"));
  }
}

// ---------------------------------------------------------------------------
// 3. Event Segmentation Filter Tests (esb/esw)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Event Segmentation Filter") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Event segmentation blacklist only affects specific events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"esb", {{"event1", json::array({"blocked_for_event1"})}}}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // event1: "blocked_for_event1" should be removed
    cly::Event e1("event1", 1);
    e1.addSegmentation("blocked_for_event1", "v1");
    e1.addSegmentation("allowed", "v2");
    countly.addEvent(e1);

    // event2: same segmentation keys, but filter should NOT apply
    cly::Event e2("event2", 1);
    e2.addSegmentation("blocked_for_event1", "v1");
    e2.addSegmentation("other", "v2");
    countly.addEvent(e2);

    countly.processRQDebug();

    // We expect 2 separate requests (eqs=1)
    CHECK(http_call_queue.size() == 2);

    // First request: event1
    HTTPCall call1 = popCall();
    json events1 = json::parse(call1.data["events"]);
    CHECK(events1.size() == 1);
    CHECK(events1[0]["key"].get<std::string>() == "event1");
    json seg1 = events1[0]["segmentation"];
    CHECK_FALSE(seg1.contains("blocked_for_event1"));
    CHECK(seg1.contains("allowed"));
    CHECK(seg1["allowed"].get<std::string>() == "v2");

    // Second request: event2
    HTTPCall call2 = popCall();
    json events2 = json::parse(call2.data["events"]);
    CHECK(events2.size() == 1);
    CHECK(events2[0]["key"].get<std::string>() == "event2");
    json seg2 = events2[0]["segmentation"];
    // Filter does not apply to event2, so both keys remain
    CHECK(seg2.contains("blocked_for_event1"));
    CHECK(seg2.contains("other"));
    CHECK(seg2["blocked_for_event1"].get<std::string>() == "v1");
    CHECK(seg2["other"].get<std::string>() == "v2");
  }

  SUBCASE("Event segmentation whitelist only keeps specific keys per event") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"esw", {{"event1", json::array({"keep_this"})}}}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event event("event1", 1);
    event.addSegmentation("keep_this", "v1");
    event.addSegmentation("remove_this", "v2");
    countly.addEvent(event);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "event1");

    json seg = events[0]["segmentation"];
    CHECK(seg.contains("keep_this"));
    CHECK(seg["keep_this"].get<std::string>() == "v1");
    CHECK_FALSE(seg.contains("remove_this"));
  }
}

// ---------------------------------------------------------------------------
// 4. User Property Filter Tests
// ---------------------------------------------------------------------------

TEST_CASE("SBS User Property Filter") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("User property blacklist blocks matching custom properties") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"upb", json::array({"blocked_prop"})}};
    initWithSBSConfig(sbs, countly);

    countly.setCustomUserDetails({{"blocked_prop", "v1"}, {"allowed_prop", "v2"}});
    countly.processRQDebug();

    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();

    json userDetails = json::parse(call.data["user_details"]);
    json custom = userDetails["custom"];
    CHECK(custom.contains("allowed_prop"));
    CHECK(custom["allowed_prop"].get<std::string>() == "v2");
    CHECK_FALSE(custom.contains("blocked_prop"));
  }

  SUBCASE("User property whitelist only allows listed properties") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"upw", json::array({"allowed_prop"})}};
    initWithSBSConfig(sbs, countly);

    countly.setCustomUserDetails({{"allowed_prop", "v1"}, {"blocked_prop", "v2"}});
    countly.processRQDebug();

    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();

    json userDetails = json::parse(call.data["user_details"]);
    json custom = userDetails["custom"];
    CHECK(custom.contains("allowed_prop"));
    CHECK(custom["allowed_prop"].get<std::string>() == "v1");
    CHECK_FALSE(custom.contains("blocked_prop"));
  }

  SUBCASE("setUserDetails (named properties) bypasses user property filter") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"upb", json::array({"name"})}};
    initWithSBSConfig(sbs, countly);

    countly.setUserDetails({{"name", "John"}});
    countly.processRQDebug();

    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();

    json userDetails = json::parse(call.data["user_details"]);
    // Named properties (like "name") should bypass the user property filter
    CHECK(userDetails.contains("name"));
    CHECK(userDetails["name"].get<std::string>() == "John");
  }
}

// ---------------------------------------------------------------------------
// 5. SBS Config Sanitization Tests
// ---------------------------------------------------------------------------

TEST_CASE("SBS Config Sanitization") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Invalid filter types are removed") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide eb as a number instead of an array => should be sanitized away
    // Provide esb as an array instead of an object => should be sanitized away
    // Provide a valid tracking boolean so we can confirm SBS was processed
    // Set eqs to 1 so events are flushed to RQ on each addEvent
    json sbs = {{"eb", 42}, {"esb", json::array({"not_an_object"})}, {"tracking", true}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Since invalid filter types are removed during sanitization,
    // events should not be filtered at all. Record events and verify they pass.
    cly::Event e1("any_event", 1);
    countly.addEvent(e1);

    cly::Event e2("another_event", 1);
    countly.addEvent(e2);

    countly.processRQDebug();

    // Collect all events across HTTP calls
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 2);
  }

  SUBCASE("Default SBS values when no config provided") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Init without SBS config, set eqs to 1 so events are flushed to RQ
    countly.setEventsToRQThreshold(1);
    countly.disableSDKBehaviorSettingsUpdates();
    countly.setHTTPClient(test_utils::fakeSendHTTP);
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableManualSessionControl();
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    // Wait briefly for the async SBS config fetch to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    countly.processRQDebug();
    countly.clearRequestQueue();
    http_call_queue.clear();

    // Without any SBS config, all events should pass through unfiltered
    cly::Event e1("event_a", 1);
    e1.addSegmentation("seg_key", "seg_val");
    countly.addEvent(e1);

    cly::Event e2("event_b", 1);
    countly.addEvent(e2);

    countly.processRQDebug();

    // Collect all events, verify segmentation is intact
    int totalEvents = 0;
    bool foundSegKey = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          totalEvents++;
          if (e.contains("segmentation") && e["segmentation"].contains("seg_key")) {
            CHECK(e["segmentation"]["seg_key"].get<std::string>() == "seg_val");
            foundSegKey = true;
          }
        }
      }
    }
    CHECK(totalEvents == 2);
    CHECK(foundSegKey);
  }

  SUBCASE("Invalid boolean types are removed") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide boolean keys with non-boolean values => should be sanitized
    json sbs = {{"tracking", "not_bool"}, {"networking", 42}, {"st", json::array()}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Since invalid types are removed, defaults should apply (all enabled)
    // Session should still work
    CHECK(countly.beginSession());
    countly.processRQDebug();

    // beginSession creates a request, networking should still be on
    CHECK(!http_call_queue.empty());
  }

  SUBCASE("Invalid numeric types are removed") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide numeric keys with non-numeric values => should be sanitized
    json sbs = {{"eqs", "not_number"}, {"rqs", false}, {"sui", json::array()}};
    initWithSBSConfig(sbs, countly);

    // Defaults should apply - default EQ threshold
    test_utils::generateEvents(5, countly);
    CHECK(countly.checkEQSize() == 5); // default threshold is 100, so 5 should still be in EQ
  }
}

// ---------------------------------------------------------------------------
// 6. Feature Flags Tests (st, cet, vt, lt, crt)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Feature Flags") {
  clearSDK();
  http_call_queue.clear();

  // --- Session Tracking (st) ---

  SUBCASE("Session tracking disabled blocks beginSession") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"st", false}};
    initWithSBSConfig(sbs, countly);

    // beginSession should fail when session tracking is disabled
    CHECK(countly.beginSession() == false);

    countly.processRQDebug();
    // No session request should be in the queue
    bool hasBeginSession = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("begin_session") != call.data.end()) {
        hasBeginSession = true;
      }
    }
    CHECK_FALSE(hasBeginSession);
  }

  SUBCASE("Session tracking disabled blocks updateSession") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"st", false}};
    initWithSBSConfig(sbs, countly);

    // updateSession should fail when session tracking is disabled
    CHECK(countly.updateSession() == false);
  }

  SUBCASE("Session tracking enabled allows session operations") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"st", true}};
    initWithSBSConfig(sbs, countly);

    // beginSession should succeed
    CHECK(countly.beginSession() == true);

    countly.processRQDebug();
    bool hasBeginSession = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("begin_session") != call.data.end()) {
        hasBeginSession = true;
      }
    }
    CHECK(hasBeginSession);
  }

  // --- Custom Event Tracking (cet) ---

  SUBCASE("Custom event tracking disabled blocks custom events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Custom events should be blocked
    cly::Event e1("custom_event", 1);
    countly.addEvent(e1);

    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("Custom event tracking disabled allows internal events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Internal events ([CLY]_ prefix) should still pass through
    std::string viewId = countly.views().openView("test_view");
    CHECK(!viewId.empty());

    countly.processRQDebug();
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          totalEvents++;
          // Should be a view event
          CHECK(e["key"].get<std::string>().find("[CLY]_") == 0);
        }
      }
    }
    CHECK(totalEvents >= 1);
  }

  SUBCASE("Custom event tracking enabled allows all events") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"cet", true}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e1("my_custom_event", 1);
    countly.addEvent(e1);

    countly.processRQDebug();
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 1);
  }

  // --- View Tracking (vt) ---

  SUBCASE("View tracking disabled blocks view operations") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"vt", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // openView should return empty string when view tracking is disabled
    std::string viewId = countly.views().openView("test_view");
    CHECK(viewId.empty());

    // No view events should be in the queue
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("View tracking enabled allows view operations") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"vt", true}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    std::string viewId = countly.views().openView("test_view");
    CHECK(!viewId.empty());

    countly.processRQDebug();
    bool hasViewEvent = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          if (e["key"].get<std::string>() == "[CLY]_view") {
            hasViewEvent = true;
          }
        }
      }
    }
    CHECK(hasViewEvent);
  }

  // --- Location Tracking (lt) ---

  SUBCASE("Location tracking disabled blocks setLocation") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"lt", false}};
    initWithSBSConfig(sbs, countly);

    // setLocation should be blocked
    countly.setLocation("US", "New York", "40.7128,-74.0060", "192.168.1.1");

    countly.processRQDebug();
    // No location request should appear
    bool hasLocationRequest = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("country_code") != call.data.end() || call.data.find("location") != call.data.end()) {
        hasLocationRequest = true;
      }
    }
    CHECK_FALSE(hasLocationRequest);
  }

  SUBCASE("Location tracking enabled allows setLocation") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"lt", true}};
    initWithSBSConfig(sbs, countly);

    countly.setLocation("US", "New York", "40.7128,-74.0060", "192.168.1.1");

    countly.processRQDebug();
    bool hasLocationRequest = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("country_code") != call.data.end()) {
        hasLocationRequest = true;
        CHECK(call.data["country_code"] == "US");
      }
    }
    CHECK(hasLocationRequest);
  }

  // --- Crash Reporting (crt) ---

  SUBCASE("Crash reporting disabled blocks recordException") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"crt", false}};
    initWithSBSConfig(sbs, countly);

    countly.crash().recordException("Test crash", "stack trace line 1\nline 2", false, {{"_os", "TestOS"}}, {});

    countly.processRQDebug();
    // No crash request should appear
    bool hasCrashRequest = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("crash") != call.data.end()) {
        hasCrashRequest = true;
      }
    }
    CHECK_FALSE(hasCrashRequest);
  }

  SUBCASE("Crash reporting enabled allows recordException") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"crt", true}};
    initWithSBSConfig(sbs, countly);

    countly.crash().recordException("Test crash", "stack trace line 1\nline 2", false, {{"_os", "TestOS"}}, {});

    countly.processRQDebug();
    bool hasCrashRequest = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("crash") != call.data.end()) {
        hasCrashRequest = true;
      }
    }
    CHECK(hasCrashRequest);
  }
}

// ---------------------------------------------------------------------------
// 7. Global Flags Tests (tracking, networking)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Global Flags") {
  clearSDK();
  http_call_queue.clear();

  // --- Tracking Flag ---

  SUBCASE("Tracking disabled blocks all requests from being queued") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"tracking", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Events can still be added to EQ, but when sent to RQ,
    // the request module should reject them since tracking is off
    cly::Event e1("test_event", 1);
    countly.addEvent(e1);

    // beginSession adds a request via requestModule->addRequestToQueue
    // which checks isTrackingEnabled
    countly.beginSession();

    countly.processRQDebug();
    // No requests should have made it to the HTTP call queue
    CHECK(http_call_queue.empty());
  }

  SUBCASE("Tracking enabled allows requests") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"tracking", true}};
    initWithSBSConfig(sbs, countly);

    countly.beginSession();
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
  }

  // --- Networking Flag ---

  SUBCASE("Networking disabled blocks request queue processing") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"networking", false}};
    initWithSBSConfig(sbs, countly);

    // Requests can still be queued, but processQueue should not send them
    countly.beginSession();

    // Process RQ - with networking disabled, requests should stay in queue
    countly.processRQDebug();
    // http_call_queue should be empty since networking is disabled
    CHECK(http_call_queue.empty());

    // But RQ should still have the request
    CHECK(countly.checkRQSize() > 0);
  }

  SUBCASE("Networking enabled allows request queue processing") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"networking", true}};
    initWithSBSConfig(sbs, countly);

    countly.beginSession();
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
  }
}

// ---------------------------------------------------------------------------
// 8. Queue Size Overrides Tests (eqs, rqs)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Queue Size Overrides") {
  clearSDK();
  http_call_queue.clear();

  // --- Event Queue Size (eqs) ---

  SUBCASE("SBS eqs overrides default event queue threshold") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    // Set SBS eqs to 5 (much smaller than default 100)
    json sbs = {{"eqs", 5}};
    initWithSBSConfig(sbs, countly);

    // Generate 7 events
    test_utils::generateEvents(7, countly);

    // With threshold 5, first 5 should have been flushed to RQ, 2 left in EQ
    CHECK(countly.checkEQSize() == 2);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 5);
  }

  SUBCASE("SBS eqs of 1 flushes every event immediately") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    test_utils::generateEvents(3, countly);

    // Each event triggers a flush, so EQ should be empty
    CHECK(countly.checkEQSize() == 0);

    countly.processRQDebug();
    // Should have 3 separate request entries
    CHECK(http_call_queue.size() == 3);
  }

  SUBCASE("SBS eqs overrides developer-set event queue threshold") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    // Developer sets threshold to 50, but SBS overrides to 3
    countly.setEventsToRQThreshold(50);
    json sbs = {{"eqs", 3}};
    initWithSBSConfig(sbs, countly);

    test_utils::generateEvents(5, countly);

    // SBS eqs=3 should override developer's 50
    CHECK(countly.checkEQSize() == 2); // 5 - 3 = 2 remaining
  }

  // --- Request Queue Size (rqs) ---

  SUBCASE("SBS rqs limits request queue size by dropping oldest") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    // Set rqs to 3, so only 3 requests can be in RQ at a time
    json sbs = {{"rqs", 3}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Generate events that flush to RQ (with eqs=1, each event is a request)
    test_utils::generateEvents(5, countly);

    // RQ should be capped at 3 (oldest 2 dropped)
    CHECK(countly.checkRQSize() <= 3);
  }
}

// ---------------------------------------------------------------------------
// 9. Combined Behavior Tests (provided SBS config)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Combined Behavior") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Provided SBS config is applied on init") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide SBS that disables custom event tracking
    json sbs = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Custom events should be blocked
    cly::Event e("custom_event", 1);
    countly.addEvent(e);
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("Multiple SBS flags work together") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Disable session tracking and view tracking, but keep events enabled
    json sbs = {{"st", false}, {"vt", false}, {"cet", true}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Session should be blocked
    CHECK(countly.beginSession() == false);

    // Views should be blocked
    std::string viewId = countly.views().openView("test");
    CHECK(viewId.empty());

    // Custom events should still work
    cly::Event e("my_event", 1);
    countly.addEvent(e);

    countly.processRQDebug();
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 1);
  }

  SUBCASE("All features disabled blocks everything") {
    clearSDK();
    Countly &countly = Countly::getInstance();

    json sbs = {
        {"tracking", false}, {"networking", false}, {"st", false}, {"vt", false},
        {"lt", false},       {"cet", false},        {"crt", false}};
    initWithSBSConfig(sbs, countly);

    // Nothing should work
    CHECK(countly.beginSession() == false);
    CHECK(countly.views().openView("test").empty());

    cly::Event e("event", 1);
    countly.addEvent(e);
    CHECK(countly.checkEQSize() == 0);

    countly.crash().recordException("crash", "trace", false, {{"_os", "TestOS"}}, {});
    countly.setLocation("US", "NY", "40,-74", "1.2.3.4");

    countly.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("All filter types correctly parsed and applied together") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {
        {"eb", json::array({"blocked_event"})},
        {"sb", json::array({"blocked_seg"})},
        {"esb", {{"special_event", json::array({"special_blocked"})}}},
        {"upb", json::array({"blocked_prop"})},
        {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Verify event blacklist works: blocked_event should be dropped
    cly::Event blockedEvt("blocked_event", 1);
    countly.addEvent(blockedEvt);

    // Verify allowed event passes with segmentation filter applied
    cly::Event allowedEvt("allowed_event", 1);
    allowedEvt.addSegmentation("blocked_seg", "v1");
    allowedEvt.addSegmentation("allowed_seg", "v2");
    countly.addEvent(allowedEvt);

    // Verify event-specific segmentation filter
    cly::Event specialEvt("special_event", 1);
    specialEvt.addSegmentation("special_blocked", "v1");
    specialEvt.addSegmentation("kept_key", "v2");
    countly.addEvent(specialEvt);

    countly.processRQDebug();

    // Collect all events
    std::vector<json> allEvents;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          allEvents.push_back(e);
        }
      }
    }

    // "blocked_event" should be dropped, so only 2 events remain
    CHECK(allEvents.size() == 2);

    // Find and check "allowed_event"
    bool foundAllowed = false;
    bool foundSpecial = false;
    for (const auto &evt : allEvents) {
      std::string key = evt["key"].get<std::string>();
      if (key == "allowed_event") {
        foundAllowed = true;
        json seg = evt["segmentation"];
        CHECK_FALSE(seg.contains("blocked_seg"));
        CHECK(seg.contains("allowed_seg"));
        CHECK(seg["allowed_seg"].get<std::string>() == "v2");
      } else if (key == "special_event") {
        foundSpecial = true;
        json seg = evt["segmentation"];
        CHECK_FALSE(seg.contains("special_blocked"));
        CHECK(seg.contains("kept_key"));
        CHECK(seg["kept_key"].get<std::string>() == "v2");
      }
    }
    CHECK(foundAllowed);
    CHECK(foundSpecial);

    // Verify user property blacklist works
    http_call_queue.clear();
    countly.setCustomUserDetails({{"blocked_prop", "v1"}, {"allowed_prop", "v2"}});
    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall upCall = popCall();
    json userDetails = json::parse(upCall.data["user_details"]);
    json custom = userDetails["custom"];
    CHECK_FALSE(custom.contains("blocked_prop"));
    CHECK(custom.contains("allowed_prop"));
  }
}

// ---------------------------------------------------------------------------
// 10. Scenario Tests (init defaults -> works -> re-init disabled -> blocked)
// ---------------------------------------------------------------------------

TEST_CASE("SBS Scenarios") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("scenario_customEventTrackingDisabled") {
    // Step 1: Init with defaults (all features enabled)
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"eqs", 1}};
    initWithSBSConfig(sbs1, countly1);

    // Record a custom event - should succeed
    cly::Event e1("test_event", 1);
    countly1.addEvent(e1);

    countly1.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call1 = popCall();
    json events1 = json::parse(call1.data["events"]);
    CHECK(events1.size() == 1);
    CHECK(events1[0]["key"].get<std::string>() == "test_event");
    http_call_queue.clear();

    // Also verify view tracking is not affected
    std::string viewId = countly1.views().openView("test_view");
    CHECK(!viewId.empty());
    countly1.processRQDebug();
    http_call_queue.clear();

    // Step 2: Re-init with custom event tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs2, countly2);

    // Custom events should now be blocked
    cly::Event e2("blocked_event", 1);
    countly2.addEvent(e2);
    CHECK(countly2.checkEQSize() == 0);

    // Views should still work (cet only blocks custom events)
    std::string viewId2 = countly2.views().openView("another_view");
    CHECK(!viewId2.empty());
  }

  SUBCASE("scenario_viewTrackingDisabled") {
    // Step 1: Init with defaults
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"eqs", 1}};
    initWithSBSConfig(sbs1, countly1);

    // Record a view - should succeed
    std::string viewId1 = countly1.views().openView("test_view");
    CHECK(!viewId1.empty());

    // Record a custom event - should succeed
    cly::Event e1("test_event", 1);
    countly1.addEvent(e1);
    countly1.processRQDebug();
    http_call_queue.clear();

    // Step 2: Re-init with view tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"vt", false}, {"eqs", 1}};
    initWithSBSConfig(sbs2, countly2);

    // Views should now be blocked
    std::string viewId2 = countly2.views().openView("test_view_2");
    CHECK(viewId2.empty());
    CHECK(countly2.checkEQSize() == 0);

    // Custom events should still work
    cly::Event e2("custom_event", 1);
    countly2.addEvent(e2);
    countly2.processRQDebug();

    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 1);
  }

  SUBCASE("scenario_trackingDisabled") {
    // Step 1: Init with defaults
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = json::object();
    initWithSBSConfig(sbs1, countly1);

    // beginSession should succeed and produce a request
    CHECK(countly1.beginSession() == true);
    countly1.processRQDebug();
    CHECK(!http_call_queue.empty());
    http_call_queue.clear();

    // Step 2: Re-init with tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"tracking", false}};
    initWithSBSConfig(sbs2, countly2);

    // beginSession attempt - RQ should remain empty since tracking is off
    countly2.beginSession();
    countly2.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("scenario_networkingDisabled") {
    // Step 1: Init with defaults
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = json::object();
    initWithSBSConfig(sbs1, countly1);

    // beginSession + processRQ should send to HTTP
    CHECK(countly1.beginSession() == true);
    countly1.processRQDebug();
    CHECK(!http_call_queue.empty());
    http_call_queue.clear();

    // Step 2: Re-init with networking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"networking", false}};
    initWithSBSConfig(sbs2, countly2);

    // beginSession queues a request, but processRQ should NOT send it
    countly2.beginSession();
    countly2.processRQDebug();
    CHECK(http_call_queue.empty());

    // But the request should still be in the RQ
    CHECK(countly2.checkRQSize() > 0);
  }

  SUBCASE("scenario_sessionTrackingDisabled") {
    // Step 1: Init with defaults
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = json::object();
    initWithSBSConfig(sbs1, countly1);

    // beginSession should succeed
    CHECK(countly1.beginSession() == true);
    countly1.processRQDebug();

    bool hasBeginSession = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("begin_session") != call.data.end()) {
        hasBeginSession = true;
      }
    }
    CHECK(hasBeginSession);

    // Step 2: Re-init with session tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"st", false}};
    initWithSBSConfig(sbs2, countly2);

    // beginSession should be blocked
    CHECK(countly2.beginSession() == false);
    countly2.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("scenario_sessionTrackingDisabled_manualSessions") {
    // Step 1: Init with defaults and manual session control
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = json::object();
    initWithSBSConfig(sbs1, countly1);

    // All session operations should succeed
    CHECK(countly1.beginSession() == true);
    CHECK(countly1.updateSession() == true);
    CHECK(countly1.endSession() == true);
    countly1.processRQDebug();
    http_call_queue.clear();

    // Step 2: Re-init with session tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"st", false}};
    initWithSBSConfig(sbs2, countly2);

    // All session operations should be blocked
    CHECK(countly2.beginSession() == false);
    CHECK(countly2.updateSession() == false);
    CHECK(countly2.endSession() == false);

    countly2.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("scenario_locationTrackingDisabled") {
    // Step 1: Init with defaults
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = json::object();
    initWithSBSConfig(sbs1, countly1);

    // setLocation should succeed
    countly1.setLocation("US", "New York", "40.7128,-74.0060", "192.168.1.1");
    countly1.processRQDebug();

    bool hasLocation = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("country_code") != call.data.end()) {
        hasLocation = true;
      }
    }
    CHECK(hasLocation);

    // Step 2: Re-init with location tracking disabled
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"lt", false}};
    initWithSBSConfig(sbs2, countly2);

    // setLocation should be blocked
    countly2.setLocation("UK", "London", "51.5074,-0.1278", "10.0.0.1");
    countly2.processRQDebug();

    bool hasLocation2 = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("country_code") != call.data.end()) {
        hasLocation2 = true;
      }
    }
    CHECK_FALSE(hasLocation2);
  }

  SUBCASE("scenario_filterConfigurationRuntimeUpdate") {
    // Init with no filters -> events pass -> re-init with filter -> events blocked
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"eqs", 1}};
    initWithSBSConfig(sbs1, countly1);

    // Events should pass unfiltered
    cly::Event e1("my_event", 1);
    countly1.addEvent(e1);

    countly1.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call1 = popCall();
    json events1 = json::parse(call1.data["events"]);
    CHECK(events1.size() == 1);
    CHECK(events1[0]["key"].get<std::string>() == "my_event");
    http_call_queue.clear();

    // Re-init with a blacklist that blocks "my_event"
    Countly::halt();
    remove(TEST_DATABASE_NAME);

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"eb", json::array({"my_event"})}, {"eqs", 1}};
    initWithSBSConfig(sbs2, countly2);

    // Same event should now be blocked
    cly::Event e2("my_event", 1);
    countly2.addEvent(e2);

    countly2.processRQDebug();

    // Only check for "my_event" in any queued events - it should not appear
    bool foundBlockedEvent = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          if (e["key"].get<std::string>() == "my_event") {
            foundBlockedEvent = true;
          }
        }
      }
    }
    CHECK_FALSE(foundBlockedEvent);
  }
}

// ---------------------------------------------------------------------------
// 11. Edge Cases & Guards
// ---------------------------------------------------------------------------

TEST_CASE("SBS Edge Cases") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Empty event whitelist allows all (empty = no filtering)") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    // Empty whitelist means "allow nothing" - but current implementation treats
    // empty filter list as "no filtering" (everything allowed).
    // This test documents the actual behavior.
    json sbs = {{"ew", json::array()}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e("some_event", 1);
    countly.addEvent(e);

    // Empty filter list = no filtering, so the event should pass through
    countly.processRQDebug();
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 1);
  }

  SUBCASE("Empty segmentation whitelist allows all keys (empty = no filtering)") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sw", json::array()}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    cly::Event e("test_event", 1);
    e.addSegmentation("key1", "v1");
    e.addSegmentation("key2", "v2");
    countly.addEvent(e);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    json seg = events[0]["segmentation"];
    CHECK(seg.contains("key1"));
    CHECK(seg.contains("key2"));
  }

  SUBCASE("Empty user property whitelist allows all properties (empty = no filtering)") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"upw", json::array()}};
    initWithSBSConfig(sbs, countly);

    countly.setCustomUserDetails({{"prop1", "v1"}, {"prop2", "v2"}});
    countly.processRQDebug();

    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json userDetails = json::parse(call.data["user_details"]);
    json custom = userDetails["custom"];
    CHECK(custom.contains("prop1"));
    CHECK(custom.contains("prop2"));
  }

  SUBCASE("setSDKBehaviorSettings rejected after init") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Custom events should be blocked
    cly::Event e1("custom_event", 1);
    countly.addEvent(e1);
    CHECK(countly.checkEQSize() == 0);

    // Try to override SBS after init - should be rejected
    std::string newSbs = json({{"cet", true}}).dump();
    countly.setSDKBehaviorSettings(newSbs);

    // Custom events should still be blocked (post-init change rejected)
    cly::Event e2("custom_event_2", 1);
    countly.addEvent(e2);
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("disableSDKBehaviorSettingsUpdates rejected after init") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"cet", true}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Should log warning and be ignored after init
    countly.disableSDKBehaviorSettingsUpdates();

    // SDK should still function normally
    cly::Event e("test", 1);
    countly.addEvent(e);

    countly.processRQDebug();
    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        totalEvents += events.size();
      }
    }
    CHECK(totalEvents == 1);
  }

  SUBCASE("Special characters in event names handled by blacklist") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {
        {"eb", json::array({"event with spaces", "event-with-dashes", "event_with_underscores"})},
        {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // These should all be blocked
    cly::Event e1("event with spaces", 1);
    countly.addEvent(e1);
    cly::Event e2("event-with-dashes", 1);
    countly.addEvent(e2);
    cly::Event e3("event_with_underscores", 1);
    countly.addEvent(e3);

    // This should pass
    cly::Event e4("normal_event", 1);
    countly.addEvent(e4);

    countly.processRQDebug();

    int totalEvents = 0;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end() && !call.data["events"].empty()) {
        json events = json::parse(call.data["events"]);
        for (const auto &e : events) {
          std::string key = e["key"].get<std::string>();
          CHECK(key != "event with spaces");
          CHECK(key != "event-with-dashes");
          CHECK(key != "event_with_underscores");
          totalEvents++;
        }
      }
    }
    CHECK(totalEvents == 1);
  }

  SUBCASE("Empty segmentation map with segmentation filter works") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sb", json::array({"some_key"})}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Event with no segmentation at all
    cly::Event e("test_event", 1);
    countly.addEvent(e);

    countly.processRQDebug();
    CHECK(!http_call_queue.empty());
    HTTPCall call = popCall();
    json events = json::parse(call.data["events"]);
    CHECK(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "test_event");
  }

  SUBCASE("Tracking disabled blocks all queue writes") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"tracking", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Try various operations that should all be blocked
    cly::Event e1("event1", 1);
    countly.addEvent(e1);

    cly::Event e2("event2", 1);
    countly.addEvent(e2);

    countly.crash().recordException("crash", "trace", false, {{"_os", "TestOS"}}, {});

    countly.setLocation("US", "NY", "40,-74", "1.2.3.4");

    countly.beginSession();

    countly.processRQDebug();

    // Nothing should have made it to the HTTP queue
    CHECK(http_call_queue.empty());
  }
}

// ---------------------------------------------------------------------------
// 12. Storage Behavior Tests (SQLite only - persistence requires database)
// ---------------------------------------------------------------------------
#ifdef COUNTLY_USE_SQLITE

TEST_CASE("SBS Storage Behavior") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Stored SBS is loaded on re-init without provided SBS") {
    // Step 1: Init SDK with provided SBS that disables custom event tracking
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs = {{"cet", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly1);

    // Verify that custom events are blocked in this session
    cly::Event e1("custom_event", 1);
    countly1.addEvent(e1);
    CHECK(countly1.checkEQSize() == 0);

    // Step 2: halt() resets the singleton but preserves the database
    Countly::halt();
    http_call_queue.clear();

    // Step 3: Re-init SDK WITHOUT providing SBS => stored SBS should be loaded from DB
    Countly &countly2 = Countly::getInstance();
    initWithoutSBSConfig(countly2);

    // Step 4: Verify the stored config is applied (cet=false should still be active)
    cly::Event e2("custom_event", 1);
    countly2.addEvent(e2);
    CHECK(countly2.checkEQSize() == 0);

    // Clean up: remove the database for subsequent tests
    Countly::halt();
    remove(TEST_DATABASE_NAME);
  }

  SUBCASE("Stored SBS takes precedence over provided SBS") {
    // Step 1: Init SDK with SBS that disables session tracking (st=false)
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"st", false}};
    initWithSBSConfig(sbs1, countly1);

    // Verify session tracking is disabled
    CHECK(countly1.beginSession() == false);

    // Step 2: halt() resets singleton, database persists with stored SBS
    Countly::halt();
    http_call_queue.clear();

    // Step 3: Re-init SDK WITH a different SBS that enables session tracking (st=true)
    // Stored SBS (st=false) should take precedence over the newly provided one
    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"st", true}};
    initWithSBSConfig(sbs2, countly2);

    // Step 4: Verify stored config takes precedence (st should still be false)
    CHECK(countly2.beginSession() == false);

    // Clean up
    Countly::halt();
    remove(TEST_DATABASE_NAME);
  }

  SUBCASE("Provided SBS is used when no stored SBS exists") {
    // Step 1: Start with a clean database (no stored SBS)
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide SBS that disables view tracking
    json sbs = {{"vt", false}, {"eqs", 1}};
    initWithSBSConfig(sbs, countly);

    // Views should be blocked since provided SBS is used (no stored SBS)
    std::string viewId = countly.views().openView("test_view");
    CHECK(viewId.empty());
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("User config defaults are used when no SBS is stored or provided") {
    // Step 1: Start with a clean database (no stored SBS) and do NOT provide SBS
    clearSDK();
    Countly &countly = Countly::getInstance();
    initWithoutSBSConfig(countly);

    // Without any SBS (stored or provided), defaults should apply:
    // - All features enabled
    // - No event/segmentation/user property filters
    // - Default EQ threshold

    // Session should work (st defaults to true)
    CHECK(countly.beginSession() == true);
    countly.processRQDebug();
    bool hasBeginSession = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("begin_session") != call.data.end()) {
        hasBeginSession = true;
      }
    }
    CHECK(hasBeginSession);

    // Views should work (vt defaults to true)
    std::string viewId = countly.views().openView("test_view");
    CHECK(!viewId.empty());
  }

  SUBCASE("Empty SBS config value uses defaults") {
    // If no value is sent for a configuration (c is empty),
    // then the SDK uses its own default or the value provided by the developer
    clearSDK();
    Countly &countly = Countly::getInstance();

    // Provide SBS with only eqs set; all other fields are absent (empty)
    // This means the SDK should use defaults for all unset fields
    json sbs = {{"eqs", 3}};
    initWithSBSConfig(sbs, countly);

    // Session tracking should default to enabled
    CHECK(countly.beginSession() == true);
    countly.processRQDebug();
    http_call_queue.clear();

    // View tracking should default to enabled
    std::string viewId = countly.views().openView("test_view");
    CHECK(!viewId.empty());

    // Custom event tracking should default to enabled
    cly::Event e("custom_event", 1);
    countly.addEvent(e);
    CHECK(countly.checkEQSize() > 0);
  }

  SUBCASE("Stored SBS feature flag persists across multiple re-inits") {
    // Step 1: Init with SBS that disables crash reporting and sets eqs=2
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs = {{"crt", false}, {"eqs", 2}};
    initWithSBSConfig(sbs, countly1);

    // Verify crash reporting is disabled
    countly1.crash().recordException("crash1", "trace1", false, {{"_os", "TestOS"}}, {});
    countly1.processRQDebug();
    bool hasCrash = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("crash") != call.data.end()) {
        hasCrash = true;
      }
    }
    CHECK_FALSE(hasCrash);

    // Step 2: First re-init without providing SBS
    Countly::halt();
    http_call_queue.clear();
    Countly &countly2 = Countly::getInstance();
    initWithoutSBSConfig(countly2);

    // Verify crash reporting is still disabled from stored SBS
    countly2.crash().recordException("crash2", "trace2", false, {{"_os", "TestOS"}}, {});
    countly2.processRQDebug();
    hasCrash = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("crash") != call.data.end()) {
        hasCrash = true;
      }
    }
    CHECK_FALSE(hasCrash);

    // Step 3: Second re-init without providing SBS
    Countly::halt();
    http_call_queue.clear();
    Countly &countly3 = Countly::getInstance();
    initWithoutSBSConfig(countly3);

    // Verify crash reporting is still disabled after a second re-init
    countly3.crash().recordException("crash3", "trace3", false, {{"_os", "TestOS"}}, {});
    countly3.processRQDebug();
    hasCrash = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("crash") != call.data.end()) {
        hasCrash = true;
      }
    }
    CHECK_FALSE(hasCrash);

    // Clean up
    Countly::halt();
    remove(TEST_DATABASE_NAME);
  }
}

// ---------------------------------------------------------------------------
// 13. Location Auto-Clearing and Clearing When Disabled
// ---------------------------------------------------------------------------

TEST_CASE("SBS Location Clearing") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("setLocation clearing (all empty) is allowed when lt=false") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"lt", false}};
    initWithSBSConfig(sbs, countly);

    // Setting actual location should be blocked
    countly.setLocation("US", "New York", "40.7,-74.0", "1.2.3.4");
    countly.processRQDebug();
    bool hasLocationSet = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("country_code") != call.data.end() && call.data["country_code"] == "US") {
        hasLocationSet = true;
      }
    }
    CHECK_FALSE(hasLocationSet);

    // But clearing location (all empty) should be allowed even when lt=false
    countly.setLocation("", "", "", "");
    countly.processRQDebug();
    bool hasClearRequest = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("location") != call.data.end() && call.data["location"].empty()) {
        hasClearRequest = true;
      }
    }
    CHECK(hasClearRequest);
  }
}

// ---------------------------------------------------------------------------
// 14. processQueue Tracking Gate (distinct from addRequestToQueue)
// ---------------------------------------------------------------------------

TEST_CASE("SBS processQueue Tracking Gate") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Queued requests are not sent when tracking is later disabled") {
    // Step 1: Init with tracking enabled, begin session to queue a request
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"tracking", true}};
    initWithSBSConfig(sbs1, countly1);

    countly1.beginSession();
    // Session request is now in the RQ

    // Step 2: Re-init with tracking disabled (stored SBS takes precedence on re-init)
    Countly::halt();
    http_call_queue.clear();

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"tracking", false}};
    initWithSBSConfig(sbs2, countly2);

    // Step 3: Process the queue — the old session request should NOT be sent
    countly2.processRQDebug();
    CHECK(http_call_queue.empty());

    Countly::halt();
    remove(TEST_DATABASE_NAME);
  }
}

// ---------------------------------------------------------------------------
// 15. Session Update Interval (sui) Override
// ---------------------------------------------------------------------------

TEST_CASE("SBS Session Update Interval Override") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("sui=1 causes session update to be sent after 1 second") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sui", 1}};
    initWithSBSConfig(sbs, countly);

    countly.beginSession();
    countly.processRQDebug();
    http_call_queue.clear();

    // Wait longer than sui (1 second)
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    countly.updateSession();
    countly.processRQDebug();

    // Session update should have been sent (duration >= sui)
    bool hasSessionUpdate = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("session_duration") != call.data.end()) {
        hasSessionUpdate = true;
      }
    }
    CHECK(hasSessionUpdate);
  }

  SUBCASE("Large sui prevents premature session updates") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    json sbs = {{"sui", 300}};
    initWithSBSConfig(sbs, countly);

    countly.beginSession();
    countly.processRQDebug();
    http_call_queue.clear();

    // Wait only 1 second (far less than sui=300)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    countly.updateSession();
    countly.processRQDebug();

    // No session update should be sent (duration < sui)
    bool hasSessionUpdate = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("session_duration") != call.data.end()) {
        hasSessionUpdate = true;
      }
    }
    CHECK_FALSE(hasSessionUpdate);
  }
}

// ---------------------------------------------------------------------------
// 16. Blacklist-to-Whitelist Transition
// ---------------------------------------------------------------------------

TEST_CASE("SBS Blacklist to Whitelist Transition") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Switching from event blacklist to whitelist works correctly") {
    // Step 1: Init with event blacklist
    clearSDK();
    Countly &countly1 = Countly::getInstance();
    json sbs1 = {{"eb", json::array({"blocked_event"})}, {"eqs", 1}};
    initWithSBSConfig(sbs1, countly1);

    // "blocked_event" should be blocked, "other_event" allowed
    cly::Event e1("blocked_event", 1);
    countly1.addEvent(e1);
    CHECK(countly1.checkEQSize() == 0); // blocked

    cly::Event e2("other_event", 1);
    countly1.addEvent(e2);
    CHECK(countly1.checkEQSize() == 0); // flushed to RQ (eqs=1)

    // Step 2: Re-init with event whitelist (no blacklist)
    Countly::halt();
    http_call_queue.clear();
    remove(TEST_DATABASE_NAME); // clear stored SBS so provided SBS takes effect

    Countly &countly2 = Countly::getInstance();
    json sbs2 = {{"ew", json::array({"allowed_only"})}, {"eqs", 1}};
    initWithSBSConfig(sbs2, countly2);

    // "allowed_only" should pass, "other_event" should be blocked by whitelist
    cly::Event e3("allowed_only", 1);
    countly2.addEvent(e3);
    CHECK(countly2.checkEQSize() == 0); // flushed (allowed + eqs=1)

    cly::Event e4("other_event", 1);
    countly2.addEvent(e4);
    CHECK(countly2.checkEQSize() == 0); // blocked by whitelist, EQ still 0

    // Verify only "allowed_only" made it to RQ
    countly2.processRQDebug();
    bool hasAllowed = false;
    bool hasOther = false;
    while (!http_call_queue.empty()) {
      HTTPCall call = popCall();
      if (call.data.find("events") != call.data.end()) {
        std::string eventsStr = call.data["events"];
        if (eventsStr.find("allowed_only") != std::string::npos) hasAllowed = true;
        if (eventsStr.find("other_event") != std::string::npos) hasOther = true;
      }
    }
    CHECK(hasAllowed);
    CHECK_FALSE(hasOther);

    Countly::halt();
    remove(TEST_DATABASE_NAME);
  }
}

// ---------------------------------------------------------------------------
// 17. Malformed SBS JSON Handling
// ---------------------------------------------------------------------------

TEST_CASE("SBS Malformed JSON Handling") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("Corrupted SBS string from config falls back to defaults") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    std::string badJson = "{this is not valid json!!!}";
    countly.setSDKBehaviorSettings(badJson);
    countly.disableSDKBehaviorSettingsUpdates();
    countly.setHTTPClient(test_utils::fakeSendHTTP);
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableManualSessionControl();
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    countly.processRQDebug();
    countly.clearRequestQueue();
    http_call_queue.clear();

    // SDK should use defaults — session tracking enabled, custom events enabled, etc.
    CHECK(countly.beginSession() == true);

    cly::Event e("test_event", 1);
    countly.addEvent(e);
    CHECK(countly.checkEQSize() > 0);
  }
}
#endif // COUNTLY_USE_SQLITE
