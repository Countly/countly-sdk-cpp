#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
/**
 * Prepares the default instance with the fake HTTP client, manual sessions and no health check request.
 */
Countly &prepareEvents(const std::string &settings = "") {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  if (!settings.empty()) {
    countly.setSDKBehaviorSettings(settings);
  }
  return countly;
}

void startEvents(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false); }

long long eventLogCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

/**
 * Moves the waiting events into a request, sends the queue and returns every event the fake server received, in order.
 */
std::vector<json> sentEvents(Countly &countly) {
  countly.flushEvents();
  countly.processRQDebug();
  std::vector<json> events;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("events")) {
      for (const json &event : json::parse(call.data["events"])) {
        events.push_back(event);
      }
    }
  }
  return events;
}

/**
 * The first sent event with the key, or null.
 */
json eventWithKey(const std::vector<json> &events, const std::string &key) {
  for (const json &event : events) {
    if (event["key"] == key) {
      return event;
    }
  }
  return json();
}
} // namespace

TEST_CASE("Invalid event values are ignored or replaced by their defaults") {
  Countly &countly = prepareEvents();
  startEvents(countly);

  countly.RecordEvent("", 1);
  countly.RecordEvent("negative_count", -1);
  countly.RecordEvent("zero_count", 0);
  countly.addEvent(cly::Event("huge_count", static_cast<size_t>(-1)));
  countly.RecordEvent("negative_duration", {}, 1, 0, -5);
  countly.RecordEvent("valid", {}, 3, -2.5, 4);

  std::vector<json> events = sentEvents(countly);
  REQUIRE(events.size() == 5);
  CHECK(eventWithKey(events, "negative_count")["count"] == 1);
  CHECK(eventWithKey(events, "zero_count")["count"] == 1);
  CHECK(eventWithKey(events, "huge_count")["count"] == 1);
  CHECK_FALSE(eventWithKey(events, "negative_duration").contains("dur"));
  json valid = eventWithKey(events, "valid");
  CHECK(valid["count"] == 3);
  CHECK(valid["sum"] == -2.5);
  CHECK(valid["dur"] == 4);
  CHECK(eventLogCode(countly, "w701") == 1);
  CHECK(eventLogCode(countly, "w750") == 4);
}

TEST_CASE("A [CLY]_ key that is no internal event is a custom event") {
  SUBCASE("the custom event switch and the event filter apply to it") {
    Countly &countly = prepareEvents("{\"cet\":false}");
    startEvents(countly);
    countly.RecordEvent("[CLY]_my_event", 1);
    countly.RecordEvent("[CLY]_view", {{"name", "Home"}, {"visit", "1"}}, 1);
    std::vector<json> events = sentEvents(countly);
    CHECK(eventWithKey(events, "[CLY]_my_event").is_null());
    CHECK_FALSE(eventWithKey(events, "[CLY]_view").is_null());
  }

  SUBCASE("the event blacklist applies to it") {
    Countly &countly = prepareEvents("{\"eb\":[\"[CLY]_my_event\"]}");
    startEvents(countly);
    countly.RecordEvent("[CLY]_my_event", 1);
    CHECK(sentEvents(countly).empty());
  }

  SUBCASE("the size limits apply to it") {
    Countly &countly = prepareEvents("{\"lkl\":20,\"lvs\":10}");
    startEvents(countly);
    countly.RecordEvent("[CLY]_" + std::string(40, 'k'), {{std::string(30, 's'), std::string(30, 'v')}}, 1);
    std::vector<json> events = sentEvents(countly);
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"] == "[CLY]_" + std::string(14, 'k'));
    CHECK(events[0]["segmentation"] == json({{std::string(20, 's'), std::string(10, 'v')}}));
  }
}

TEST_CASE("The values an app gives in an internal event follow the value size limit") {
  Countly &countly = prepareEvents("{\"lvs\":10}");
  startEvents(countly);
  countly.RecordEvent("[CLY]_nps", {{"widget_id", "123"}, {"comment", std::string(30, 'c')}}, 1);
  std::vector<json> events = sentEvents(countly);
  REQUIRE(events.size() == 1);
  CHECK(events[0]["key"] == "[CLY]_nps");
  CHECK(events[0]["segmentation"]["comment"] == std::string(10, 'c'));
  CHECK(events[0]["segmentation"]["widget_id"] == "123");
  CHECK(eventLogCode(countly, "w202") == 1);
}

TEST_CASE("Segmentation filters change only custom events") {
  SUBCASE("a segmentation whitelist keeps the view fields") {
    Countly &countly = prepareEvents("{\"sw\":[\"level\"]}");
    startEvents(countly);
    countly.beginSession();
    countly.views().openView("Home");
    countly.RecordEvent("level_done", {{"level", "3"}, {"score", "50"}}, 1);
    std::vector<json> events = sentEvents(countly);
    json view = eventWithKey(events, "[CLY]_view");
    REQUIRE_FALSE(view.is_null());
    CHECK(view["segmentation"]["name"] == "Home");
    CHECK(view["segmentation"]["visit"] == "1");
    CHECK(view["segmentation"].contains("_idv"));
    CHECK(eventWithKey(events, "level_done")["segmentation"] == json({{"level", "3"}}));
  }

  SUBCASE("a segmentation blacklist keeps the view name") {
    Countly &countly = prepareEvents("{\"sb\":[\"name\"]}");
    startEvents(countly);
    countly.views().openView("Home");
    countly.RecordEvent("signup", {{"name", "Alice"}, {"plan", "pro"}}, 1);
    std::vector<json> events = sentEvents(countly);
    CHECK(eventWithKey(events, "[CLY]_view")["segmentation"]["name"] == "Home");
    CHECK(eventWithKey(events, "signup")["segmentation"] == json({{"plan", "pro"}}));
  }
}

TEST_CASE("Cut and dropped values are logged as warnings") {
  Countly &countly = prepareEvents("{\"lkl\":10,\"lvs\":10,\"lsv\":2}");
  startEvents(countly);
  countly.RecordEvent("a_long_event_key", {{"a_long_segmentation_key", "short"}, {"b", "a long value"}, {"c", "dropped"}}, 1);
  std::vector<json> events = sentEvents(countly);
  REQUIRE(events.size() == 1);
  CHECK(events[0]["key"] == "a_long_eve");
  CHECK(events[0]["segmentation"] == json({{"a_long_seg", "short"}, {"b", "a long val"}}));
  CHECK(eventLogCode(countly, "w201") == 2);
  CHECK(eventLogCode(countly, "w202") == 1);
  CHECK(eventLogCode(countly, "w203") == 1);
}

TEST_CASE("Events carry their own ID and the IDs that link them") {
  Countly &countly = prepareEvents();
  startEvents(countly);
  countly.beginSession();

  countly.RecordEvent("before_view", 1);
  const std::string viewId = countly.views().openView("Home");
  countly.RecordEvent("in_home", 1);
  const std::string secondViewId = countly.views().openView("Settings");
  countly.RecordEvent("in_settings", 1);
  countly.views().closeViewWithID(viewId);

  std::vector<json> events = sentEvents(countly);
  REQUIRE(events.size() == 6);

  std::set<std::string> ids;
  for (const json &event : events) {
    REQUIRE(event.contains("id"));
    ids.insert(event["id"].get<std::string>());
  }
  CHECK(ids.size() == 5); // the start and the end event of a view share its ID

  json beforeView = eventWithKey(events, "before_view");
  json inHome = eventWithKey(events, "in_home");
  json inSettings = eventWithKey(events, "in_settings");
  CHECK(beforeView["peid"] == "");
  CHECK(beforeView["cvid"] == "");
  CHECK(inHome["peid"] == beforeView["id"]);
  CHECK(inHome["cvid"] == viewId);
  CHECK(inSettings["peid"] == inHome["id"]);
  CHECK(inSettings["cvid"] == secondViewId);

  std::vector<json> views;
  for (const json &event : events) {
    if (event["key"] == "[CLY]_view") {
      CHECK_FALSE(event.contains("peid"));
      CHECK_FALSE(event.contains("cvid"));
      views.push_back(event);
    }
  }
  REQUIRE(views.size() == 3);
  CHECK(views[0]["id"] == viewId);
  CHECK(views[0]["pvid"] == "");
  CHECK(views[1]["id"] == secondViewId);
  CHECK(views[1]["pvid"] == viewId);
  CHECK(views[2]["id"] == viewId);
  CHECK(views[2]["pvid"] == "");
}

TEST_CASE("A timed event is recorded at the time it ends") {
  Countly &countly = prepareEvents();
  startEvents(countly);
  cly::Event event("timed_action", 1);
  event.startTimer();
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  const long long stopped = getUnixTimestamp();
  event.stopTimer();
  countly.addEvent(event);

  std::vector<json> events = sentEvents(countly);
  REQUIRE(events.size() == 1);
  CHECK(events[0]["timestamp"].get<long long>() >= stopped);
  CHECK(events[0]["dur"] == 1);
}

TEST_CASE("The event queue moves its events into a request at 1000 events") {
  Countly &countly = prepareEvents();
  startEvents(countly);
  countly.setEventsToRQThreshold(5000);
  for (int index = 0; index < 1000; index++) {
    countly.RecordEvent("tap", 1);
  }
  CHECK(countly.checkEQSize() == 0);
  countly.processRQDebug();
  std::vector<size_t> batches;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("events")) {
      batches.push_back(json::parse(call.data["events"]).size());
    }
  }
  CHECK(batches == std::vector<size_t>{1000});
}

TEST_CASE("Events are not recorded while the server turns tracking off") {
  Countly &countly = prepareEvents("{\"tracking\":false}");
  startEvents(countly);
  countly.RecordEvent("ignored", 1);
  CHECK(countly.checkEQSize() == 0);
  CHECK(eventLogCode(countly, "w505") == 1);
}
