#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
/**
 * The features the C++ SDK offers for consent, as the server receives their names.
 */
const std::vector<std::string> ALL_FEATURES = {"sessions", "events", "views", "location", "crashes", "users", "remote-config", "metrics", "feedback", "star-rating"};

/**
 * The consent state the SDK sends when exactly the given features have consent.
 */
json stateWith(const std::vector<std::string> &given) {
  json state = json::object();
  for (const std::string &feature : ALL_FEATURES) {
    state[feature] = false;
  }
  for (const std::string &feature : given) {
    state[feature] = true;
  }
  return state;
}

/**
 * Every feature except the given one.
 */
std::vector<std::string> allExcept(const std::string &removed) {
  std::vector<std::string> features;
  for (const std::string &feature : ALL_FEATURES) {
    if (feature != removed) {
      features.push_back(feature);
    }
  }
  return features;
}

/**
 * The SDK behavior settings server of these tests. It answers a settings fetch with 'answer', fails
 * it like the shared fake client while 'answer' is null, and holds the answer back while 'hold' is set.
 */
struct SettingsServer {
  std::mutex mutex;
  std::condition_variable changed;
  json answer;
  bool hold = false;

  void reset(const json &newAnswer = nullptr, bool holdAnswer = false) {
    std::lock_guard<std::mutex> lock(mutex);
    answer = newAnswer;
    hold = holdAnswer;
    changed.notify_all();
  }

  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    hold = false;
    changed.notify_all();
  }
};

SettingsServer settings_server;

/**
 * Releases a held settings answer when a test ends, so destroying the instance does not wait for it.
 */
struct ReleaseSettingsOnExit {
  ~ReleaseSettingsOnExit() { settings_server.release(); }
};

/**
 * The fake HTTP client of these tests: captures every call like fakeSendHTTP, answers the settings
 * fetch through the settings server, and answers a full remote config download with every value.
 */
HTTPResponse consentSendHTTP(bool use_post, const std::string &url, const std::string &data) {
  HTTPResponse response = fakeSendHTTP(use_post, url, data);
  if (url == "/o/sdk" && data.find("method=rc") != std::string::npos && data.find("keys=") == std::string::npos) {
    return HTTPResponse{true, json({{"color", "#FF9900"}, {"playerQueueTimeout", 32}}), 200, ""};
  }
  if (url != "/o/sdk" || data.find("method=sc") == std::string::npos) {
    return response;
  }
  std::unique_lock<std::mutex> lock(settings_server.mutex);
  settings_server.changed.wait_for(lock, std::chrono::seconds(10), [] { return !settings_server.hold; });
  if (settings_server.answer.is_null()) {
    return response;
  }
  return HTTPResponse{true, settings_server.answer, 200, ""};
}

/**
 * Waits until the condition holds, at most five seconds.
 */
template <typename Condition> bool waitUntil(Condition condition) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

/**
 * Sets the instance up for a consent test: the fake client, the test device ID and database, no
 * settings fetch unless the test serves settings, and an update loop that stays out of the test's way
 * and stops at once.
 */
void prepare(Countly &countly, bool fetchSettings = false) {
  countly.setHTTPClient(consentSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  if (!fetchSettings) {
    countly.disableSDKBehaviorSettingsUpdates();
  }
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(60000);
}

void start(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false); }

/**
 * Starts a fresh default instance prepared for a consent test, with the settings answer given.
 */
Countly &freshInstance(const json &settingsAnswer = nullptr, bool holdSettings = false) {
  clearSDK();
  settings_server.reset(settingsAnswer, holdSettings);
  Countly &countly = Countly::getInstance();
  prepare(countly, !settingsAnswer.is_null());
  return countly;
}

/**
 * Sends the queued requests and returns those that went to /i, oldest first. Forgets every call
 * captured so far.
 */
std::vector<HTTPCall> takeRequests(Countly &countly) {
  countly.processRQDebug();
  waitUntil([&countly] { return countly.checkRQSize() == 0; });
  std::vector<HTTPCall> requests;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.url == "/i") {
      requests.push_back(call);
    }
  }
  http_call_queue.clear();
  return requests;
}

bool has(const HTTPCall &call, const std::string &key) { return call.data.find(key) != call.data.end(); }

std::string valueOf(const HTTPCall &call, const std::string &key) {
  std::map<std::string, std::string>::const_iterator found = call.data.find(key);
  return found == call.data.end() ? "<missing>" : found->second;
}

json consentOf(const HTTPCall &call) {
  REQUIRE(has(call, "consent"));
  return json::parse(call.data.at("consent"));
}

/**
 * The events the given requests carry, in order.
 */
std::vector<json> eventsIn(const std::vector<HTTPCall> &requests) {
  std::vector<json> events;
  for (const HTTPCall &call : requests) {
    if (has(call, "events")) {
      for (const json &event : json::parse(call.data.at("events"))) {
        events.push_back(event);
      }
    }
  }
  return events;
}

/**
 * The keys of the recorded events, in order: the waiting events are moved into a request and every request is sent.
 */
std::vector<std::string> recordedEventKeys(Countly &countly) {
  countly.flushEvents();
  std::vector<std::string> keys;
  for (const json &event : eventsIn(takeRequests(countly))) {
    keys.push_back(event["key"].get<std::string>());
  }
  return keys;
}

/**
 * How many full or partial remote config downloads the fake client received since the last takeRequests.
 */
size_t remoteConfigFetches() {
  size_t count = 0;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.url == "/o/sdk" && valueOf(call, "method") == "rc") {
      count++;
    }
  }
  return count;
}

/**
 * The count of a log code in the instance's health check counters, 0 when it was not recorded.
 */
long long logCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

/**
 * Waits until the fake HTTP client received a health check, at most five seconds.
 */
bool waitForHealthCheck() {
  return waitUntil([] { return !health_check_call_queue.empty(); });
}

const std::map<std::string, std::string> CRASH_METRICS = {{"_os", "Windows"}, {"_app_version", "2.4.1"}};
} // namespace

TEST_CASE("consent: required with nothing given, only the consent state and a location erase are sent") {
  Countly &countly = freshInstance();
  countly.setRequiresConsent(true);
  countly.setMetrics("Windows", "10", "PC", "1920x1080", "", "2.4.1");
  countly.enableRemoteConfig();
  start(countly);

  std::vector<HTTPCall> requests = takeRequests(countly);
  REQUIRE(requests.size() == 2);
  CHECK(consentOf(requests[0]) == stateWith({}));
  CHECK(valueOf(requests[1], "location") == "");
  CHECK(!has(requests[1], "begin_session"));

  REQUIRE(waitForHealthCheck());
  CHECK(!has(health_check_call_queue.front(), "metrics"));

  countly.addEvent(Event("click", 1));
  countly.RecordEvent("[CLY]_view", 1);
  CHECK(countly.views().openView("main").empty());
  countly.crash().addBreadcrumb("crumb");
  countly.crash().recordException("title", "trace", false, CRASH_METRICS);
  countly.setUserDetails({{"name", "Alice"}});
  countly.setCustomUserDetails({{"level", "3"}});
  countly.multiplyCustomUserDetail("score", 2);
  countly.setLocation("TR", "Istanbul", "", "");
  countly.updateRemoteConfig();
  CHECK_FALSE(countly.beginSession());
  CHECK_FALSE(countly.updateSession());
  CHECK_FALSE(countly.endSession());

  CHECK(countly.checkEQSize() == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(remoteConfigFetches() == 0);
  CHECK(takeRequests(countly).empty());
  CHECK(countly.debugHealthCheck()->snapshot()["fu"].empty());
  clearSDK();
}

TEST_CASE("consent: giving consent sends the full state once and starts the features") {
  Countly &countly = freshInstance();
  countly.setRequiresConsent(true);
  start(countly);
  takeRequests(countly);

  countly.giveConsent({cly::consent::SESSIONS, cly::consent::EVENTS});
  std::vector<HTTPCall> requests = takeRequests(countly);
  REQUIRE(requests.size() == 2);
  CHECK(consentOf(requests[0]) == stateWith({"sessions", "events"}));
  CHECK(valueOf(requests[1], "begin_session") == "1");

  countly.addEvent(Event("click", 1));
  countly.flushEvents(std::chrono::seconds(1));
  std::vector<json> events = eventsIn(takeRequests(countly));
  REQUIRE(events.size() == 1);
  CHECK(events[0]["key"] == "click");

  countly.giveConsent({cly::consent::SESSIONS, cly::consent::EVENTS});
  CHECK(takeRequests(countly).empty());

  countly.giveConsent({cly::consent::EVENTS, cly::consent::VIEWS});
  requests = takeRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(consentOf(requests[0]) == stateWith({"sessions", "events", "views"}));
  clearSDK();
}

TEST_CASE("consent: removing consent stops the feature and runs its clean-up") {
  SUBCASE("views: every running view ends, later view calls record nothing") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(cly::consent::allFeatures());
    start(countly);
    takeRequests(countly);

    const std::string home = countly.views().openView("Home");
    countly.views().openView("Settings");
    REQUIRE(!home.empty());
    countly.removeConsent({cly::consent::VIEWS});

    std::vector<std::string> queued = countly.debugReturnStateOfEQ();
    REQUIRE(queued.size() == 4);
    std::set<std::string> ended;
    for (size_t index = 2; index < queued.size(); index++) {
      json event = json::parse(queued[index]);
      CHECK(event["key"] == "[CLY]_view");
      CHECK(!event["segmentation"].contains("visit"));
      ended.insert(event["segmentation"]["name"].get<std::string>());
    }
    CHECK(ended == std::set<std::string>({"Home", "Settings"}));

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith(allExcept("views")));

    CHECK(countly.views().openView("Profile").empty());
    countly.views().closeViewWithID(home);
    CHECK(countly.checkEQSize() == 4);
    clearSDK();
  }

  SUBCASE("sessions: the running session ends, queued events are still sent") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(cly::consent::allFeatures());
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(valueOf(requests[1], "begin_session") == "1");

    countly.addEvent(Event("before", 1));
    countly.removeConsent({cly::consent::SESSIONS});
    requests = takeRequests(countly);
    // the events recorded in the session go out before the session ends
    REQUIRE(requests.size() == 3);
    std::vector<json> events = eventsIn({requests[0]});
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"] == "before");
    CHECK(valueOf(requests[1], "end_session") == "1");
    CHECK(consentOf(requests[2]) == stateWith(allExcept("sessions")));

    CHECK_FALSE(countly.updateSession());
    countly.flushEvents(std::chrono::seconds(1));
    CHECK(takeRequests(countly).empty());
    clearSDK();
  }

  SUBCASE("location: the stored location is cleared and erased, later session starts carry the empty location") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(cly::consent::allFeatures());
    start(countly);
    // with a running session the location goes in a separate request right away
    REQUIRE(countly.beginSession());
    takeRequests(countly);

    countly.setLocation("TR", "Istanbul", "41.0082,28.9784", "");
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "country_code") == "TR");
    CHECK(valueOf(requests[0], "city") == "Istanbul");

    countly.removeConsent({cly::consent::LOCATION});
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(consentOf(requests[0]) == stateWith(allExcept("location")));
    CHECK(valueOf(requests[1], "location") == "");
    CHECK(!has(requests[1], "country_code"));
    CHECK(!has(requests[1], "city"));

    REQUIRE(countly.endSession());
    takeRequests(countly);
    REQUIRE(countly.beginSession());
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");
    CHECK(valueOf(requests[0], "location") == "");
    CHECK(!has(requests[0], "country_code"));
    CHECK(!has(requests[0], "city"));
    clearSDK();
  }

  SUBCASE("crashes: the breadcrumbs are cleared") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(cly::consent::allFeatures());
    start(countly);
    takeRequests(countly);

    countly.crash().addBreadcrumb("kept");
    countly.crash().recordException("first", "trace", false, CRASH_METRICS);
    countly.crash().addBreadcrumb("dropped");
    countly.removeConsent({cly::consent::CRASHES});
    countly.giveConsent({cly::consent::CRASHES});
    countly.crash().recordException("second", "trace", false, CRASH_METRICS);

    std::vector<json> crashes;
    for (const HTTPCall &call : takeRequests(countly)) {
      if (has(call, "crash")) {
        crashes.push_back(json::parse(call.data.at("crash")));
      }
    }
    REQUIRE(crashes.size() == 2);
    CHECK(crashes[0]["_logs"] == "kept\n");
    CHECK(crashes[1]["_logs"] == "");
    clearSDK();
  }

  SUBCASE("users: details waiting for the first session are dropped") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS, cly::consent::USERS});
    countly.setUserDetails({{"name", "Alice"}});
    start(countly);
    takeRequests(countly);

    countly.removeConsent({cly::consent::USERS});
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions"}));

    REQUIRE(countly.beginSession());
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");
    CHECK(!has(requests[0], "user_details"));
    clearSDK();
  }

  SUBCASE("users, with the consent kept: details waiting for the first session go with it") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS, cly::consent::USERS});
    countly.setUserDetails({{"name", "Alice"}});
    start(countly);
    takeRequests(countly);

    REQUIRE(countly.beginSession());
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    REQUIRE(has(requests[0], "user_details"));
    CHECK(json::parse(requests[0].data.at("user_details")) == json({{"name", "Alice"}}));
    clearSDK();
  }
}

TEST_CASE("consent: a device ID change without merge removes all consent without telling the server") {
  Countly &countly = freshInstance();
  countly.setRequiresConsent(true);
  countly.setConsentEnabled(cly::consent::allFeatures());
  start(countly);
  takeRequests(countly);

  countly.views().openView("Settings");
  countly.addEvent(Event("click", 1));
  countly.setDeviceID("new-user", false);

  std::vector<HTTPCall> requests = takeRequests(countly);
  REQUIRE(!requests.empty());
  int endSessions = 0;
  for (const HTTPCall &call : requests) {
    CHECK(valueOf(call, "device_id") == COUNTLY_TEST_DEVICE_ID);
    CHECK(!has(call, "consent"));
    CHECK(!has(call, "location"));
    if (valueOf(call, "end_session") == "1") {
      endSessions++;
    }
  }
  CHECK(endSessions == 1);
  std::vector<json> events = eventsIn(requests);
  REQUIRE(events.size() == 3);
  CHECK(events[0]["key"] == "[CLY]_view");
  CHECK(events[0]["segmentation"]["visit"] == "1");
  CHECK(events[1]["key"] == "click");
  CHECK(events[2]["key"] == "[CLY]_view");
  CHECK(events[2]["segmentation"]["name"] == "Settings");
  CHECK(!events[2]["segmentation"].contains("visit"));

  countly.addEvent(Event("ignored", 1));
  CHECK(countly.checkEQSize() == 0);
  CHECK(takeRequests(countly).empty());

  countly.giveConsent({cly::consent::SESSIONS});
  requests = takeRequests(countly);
  REQUIRE(requests.size() == 2);
  CHECK(valueOf(requests[0], "device_id") == "new-user");
  CHECK(consentOf(requests[0]) == stateWith({"sessions"}));
  CHECK(valueOf(requests[1], "begin_session") == "1");
  CHECK(valueOf(requests[1], "device_id") == "new-user");
  clearSDK();
}

TEST_CASE("consent: a device ID change without merge ends the running views under the old device ID") {
  Countly &countly = freshInstance();
  countly.disableHealthCheck();
  countly.enableManualSessionControl();
  start(countly);
  takeRequests(countly);

  countly.views().openView("Settings");
  countly.setDeviceID("new-user", false);

  std::vector<HTTPCall> requests = takeRequests(countly);
  std::vector<json> events = eventsIn(requests);
  REQUIRE(events.size() == 2);
  CHECK(events[1]["key"] == "[CLY]_view");
  CHECK(events[1]["segmentation"]["name"] == "Settings");
  CHECK(!events[1]["segmentation"].contains("visit"));
  for (const HTTPCall &call : requests) {
    CHECK(valueOf(call, "device_id") == COUNTLY_TEST_DEVICE_ID);
  }

  countly.views().closeViewWithName("Settings");
  CHECK(countly.checkEQSize() == 0);
  CHECK(logCode(countly, "w730") == 1);
  clearSDK();
}

TEST_CASE("consent: a device ID change with merge keeps consent") {
  Countly &countly = freshInstance();
  countly.enableManualSessionControl();
  countly.setRequiresConsent(true);
  countly.setConsentEnabled({cly::consent::EVENTS});
  start(countly);
  takeRequests(countly);

  countly.setDeviceID("merged-user", true);
  std::vector<HTTPCall> requests = takeRequests(countly);
  REQUIRE(requests.size() == 1);
  CHECK(valueOf(requests[0], "old_device_id") == COUNTLY_TEST_DEVICE_ID);
  CHECK(!has(requests[0], "consent"));

  CHECK(countly.getConsent(cly::consent::EVENTS));
  countly.addEvent(Event("click", 1));
  CHECK(countly.checkEQSize() == 1);
  clearSDK();
}

TEST_CASE("consent: the server's consent requirement (cr)") {
  SUBCASE("cr true turns the requirement on: the session ends and the server gets the state") {
    Countly &countly = freshInstance(json({{"v", 2}, {"t", 1}, {"c", {{"cr", true}}}}), true);
    ReleaseSettingsOnExit release;
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");
    countly.addEvent(Event("before", 1));
    CHECK(countly.getConsent(cly::consent::EVENTS));

    settings_server.release();
    REQUIRE(waitUntil([&countly] { return countly.checkRQSize() == 4; }));
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 4);
    // the events recorded in the session go out before the session ends
    std::vector<json> events = eventsIn({requests[0]});
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"] == "before");
    CHECK(valueOf(requests[1], "end_session") == "1");
    CHECK(consentOf(requests[2]) == stateWith({}));
    CHECK(valueOf(requests[3], "location") == "");
    CHECK_FALSE(countly.getConsent(cly::consent::EVENTS));

    countly.addEvent(Event("after", 1));
    countly.flushEvents(std::chrono::seconds(1));
    CHECK(eventsIn(takeRequests(countly)).empty());
    clearSDK();
  }

  SUBCASE("cr false lifts the requirement the app set, and no consent state is sent") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    std::string settings = json({{"cr", false}}).dump();
    countly.setSDKBehaviorSettings(settings);
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");
    CHECK(countly.getConsent(cly::consent::EVENTS));
    countly.addEvent(Event("click", 1));
    CHECK(countly.checkEQSize() == 1);
    clearSDK();
  }

  SUBCASE("cr false arriving while running lifts the app's requirement: the session begins and no consent state is sent") {
    Countly &countly = freshInstance(json({{"v", 2}, {"t", 1}, {"c", {{"cr", false}}}}), true);
    ReleaseSettingsOnExit release;
    countly.setRequiresConsent(true);
    countly.enableRemoteConfig();
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(consentOf(requests[0]) == stateWith({}));
    CHECK_FALSE(countly.getConsent(cly::consent::EVENTS));

    settings_server.release();
    REQUIRE(waitUntil([] { return remoteConfigFetches() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(remoteConfigFetches() == 1);
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");
    CHECK(countly.getConsent(cly::consent::EVENTS));
    clearSDK();
  }

  SUBCASE("cr leaving the settings brings back the app's requirement") {
    Countly &countly = freshInstance(json({{"v", 2}, {"t", 1}, {"c", json::object()}}), true);
    ReleaseSettingsOnExit release;
    countly.setRequiresConsent(true);
    std::string settings = json({{"cr", false}}).dump();
    countly.setSDKBehaviorSettings(settings);
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "begin_session") == "1");

    settings_server.release();
    REQUIRE(waitUntil([&countly] { return countly.checkRQSize() == 3; }));
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 3);
    CHECK(valueOf(requests[0], "end_session") == "1");
    CHECK(consentOf(requests[1]) == stateWith({}));
    CHECK(valueOf(requests[2], "location") == "");
    clearSDK();
  }
}

TEST_CASE("consent: calls that are ignored") {
  SUBCASE("before start, consent calls are ignored and record w101") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.giveConsent({cly::consent::SESSIONS});
    CHECK_FALSE(countly.getConsent(cly::consent::SESSIONS));
    CHECK(logCode(countly, "w101") == 2);

    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(!requests.empty());
    CHECK(consentOf(requests[0]) == stateWith({}));
    clearSDK();
  }

  SUBCASE("while consent is not required, consent calls change nothing and send nothing") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    start(countly);
    takeRequests(countly);

    countly.giveConsent({cly::consent::SESSIONS});
    countly.removeConsent({cly::consent::EVENTS});
    countly.removeConsentAll();
    CHECK(takeRequests(countly).empty());
    CHECK(countly.getConsent(cly::consent::EVENTS));
    countly.addEvent(Event("click", 1));
    CHECK(countly.checkEQSize() == 1);
    clearSDK();
  }

  SUBCASE("unknown names and empty lists record w402; known names in the same call still count") {
    Countly &countly = freshInstance();
    countly.disableHealthCheck();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    start(countly);
    takeRequests(countly);

    countly.giveConsent({"bogus"});
    countly.giveConsent({});
    CHECK(takeRequests(countly).empty());

    countly.giveConsent({cly::consent::SESSIONS, "bogus"});
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions"}));

    CHECK_FALSE(countly.getConsent("bogus"));
    CHECK(logCode(countly, "w402") == 4);
    clearSDK();
  }
}

TEST_CASE("consent: feature groups") {
  SUBCASE("a group gives and removes all of its features at once") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    start(countly);
    takeRequests(countly);

    countly.createFeatureGroup("activity", {cly::consent::SESSIONS, cly::consent::EVENTS, cly::consent::VIEWS});
    countly.setConsentFeatureGroup("activity", true);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions", "events", "views"}));
    CHECK(countly.getConsent("activity"));

    countly.removeConsent({cly::consent::VIEWS});
    takeRequests(countly);
    CHECK_FALSE(countly.getConsent("activity"));

    countly.giveConsent({"activity"});
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions", "events", "views"}));

    countly.setConsentFeatureGroup("activity", false);
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({}));
    clearSDK();
  }

  SUBCASE("a group needs its own name and known features, and creating it again replaces it") {
    Countly &countly = freshInstance();
    countly.disableHealthCheck();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    start(countly);
    takeRequests(countly);

    countly.createFeatureGroup("sessions", {cly::consent::EVENTS});
    countly.createFeatureGroup("", {cly::consent::EVENTS});
    countly.createFeatureGroup("nothing", {});
    countly.createFeatureGroup("unknown", {"bogus"});
    countly.setConsentFeatureGroup("nothing", true);
    CHECK(takeRequests(countly).empty());
    CHECK(logCode(countly, "w700") == 2);
    CHECK(logCode(countly, "w402") == 3);

    countly.giveConsent({"sessions"});
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions"}));

    countly.createFeatureGroup("pair", {cly::consent::EVENTS});
    countly.createFeatureGroup("pair", {cly::consent::VIEWS});
    countly.setConsentFeatureGroup("pair", true);
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions", "views"}));
    clearSDK();
  }
}

TEST_CASE("consent: metrics need the metrics consent") {
  SUBCASE("without it, the session start and the health check carry no metrics") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS});
    countly.setMetrics("Windows", "10", "PC", "1920x1080", "", "2.4.1");
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(valueOf(requests[1], "begin_session") == "1");
    CHECK(!has(requests[1], "metrics"));
    REQUIRE(waitForHealthCheck());
    CHECK(!has(health_check_call_queue.front(), "metrics"));
    clearSDK();
  }

  SUBCASE("with it, both carry their metrics") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS, cly::consent::METRICS});
    countly.setMetrics("Windows", "10", "PC", "1920x1080", "", "2.4.1");
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    REQUIRE(has(requests[1], "metrics"));
    CHECK(json::parse(requests[1].data.at("metrics")) == json({{"_os", "Windows"}, {"_os_version", "10"}, {"_device", "PC"}, {"_resolution", "1920x1080"}, {"_app_version", "2.4.1"}}));
    REQUIRE(waitForHealthCheck());
    HTTPCall healthCheck = health_check_call_queue.front();
    REQUIRE(has(healthCheck, "metrics"));
    CHECK(json::parse(healthCheck.data.at("metrics")) == json({{"_app_version", "2.4.1"}}));
    clearSDK();
  }
}

TEST_CASE("consent: remote config needs the remote-config consent") {
  SUBCASE("no download without it; giving it downloads once; the values stay after removal") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.enableRemoteConfig();
    start(countly);
    takeRequests(countly);

    countly.updateRemoteConfig();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(remoteConfigFetches() == 0);

    countly.giveConsent({cly::consent::REMOTE_CONFIG});
    REQUIRE(waitUntil([] { return remoteConfigFetches() == 1; }));
    REQUIRE(waitUntil([&countly] { return countly.getRemoteConfigValue("color") == "#FF9900"; }));

    countly.removeConsent({cly::consent::REMOTE_CONFIG});
    CHECK(countly.getRemoteConfigValue("color") == "#FF9900");
    countly.updateRemoteConfig();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(remoteConfigFetches() == 1);
    clearSDK();
  }

  SUBCASE("giving sessions and remote-config together downloads once") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.enableRemoteConfig();
    start(countly);
    takeRequests(countly);

    countly.giveConsent({cly::consent::SESSIONS, cly::consent::REMOTE_CONFIG});
    REQUIRE(waitUntil([] { return remoteConfigFetches() >= 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(remoteConfigFetches() == 1);
    clearSDK();
  }
}

TEST_CASE("consent: internal events need the consent of their own feature") {
  const std::vector<std::string> keys = {"custom", "[CLY]_unknown", "[CLY]_view", "[CLY]_nps", "[CLY]_survey", "[CLY]_star_rating", "[CLY]_orientation", "[CLY]_push_action"};

  struct Case {
    std::string name;
    bool required;
    std::vector<std::string> given;
    std::vector<std::string> recorded;
  };
  const std::vector<Case> cases = {
      {"events covers custom keys and unknown internal keys", true, {cly::consent::EVENTS}, {"custom", "[CLY]_unknown"}},
      {"feedback covers NPS, survey and rating answers", true, {cly::consent::FEEDBACK}, {"[CLY]_nps", "[CLY]_survey", "[CLY]_star_rating"}},
      {"star-rating covers rating answers only", true, {cly::consent::STAR_RATING}, {"[CLY]_star_rating"}},
      {"views and users cover their own events", true, {cly::consent::VIEWS, cly::consent::USERS}, {"[CLY]_view", "[CLY]_orientation"}},
      {"without the requirement every key is recorded", false, {}, keys},
  };

  for (const Case &testCase : cases) {
    CAPTURE(testCase.name);
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(testCase.required);
    countly.setConsentEnabled(testCase.given);
    start(countly);

    for (const std::string &key : keys) {
      countly.RecordEvent(key, 1);
    }
    CHECK(recordedEventKeys(countly) == testCase.recorded);
    clearSDK();
  }
}

TEST_CASE("consent: the health check counts only the calls of features with consent") {
  SUBCASE("calls of features without consent are not counted") {
    Countly &countly = freshInstance();
    countly.disableHealthCheck();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::EVENTS});
    start(countly);

    countly.beginSession();
    countly.updateSession();
    countly.endSession();
    countly.addEvent(Event("click", 1));
    countly.views().openView("main");
    countly.crash().addBreadcrumb("crumb");
    countly.crash().recordException("title", "trace", false, CRASH_METRICS);

    CHECK(countly.debugHealthCheck()->snapshot()["fu"] == json({{"events", {{"record", 1}}}}));
    clearSDK();
  }

  SUBCASE("w101 of a call before start is counted only with the consent of its feature") {
    Countly &countly = freshInstance();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS});
    countly.addEvent(Event("too early", 1));
    countly.beginSession();
    CHECK(logCode(countly, "w101") == 1);
    clearSDK();
  }

  SUBCASE("w101 of a call before start is counted for every feature while consent is not required") {
    Countly &countly = freshInstance();
    countly.addEvent(Event("too early", 1));
    countly.beginSession();
    CHECK(logCode(countly, "w101") == 2);
    clearSDK();
  }
}

TEST_CASE("consent: consent given before start") {
  SUBCASE("every feature the SDK offers can be given at once") {
    const std::vector<std::string> offered = cly::consent::allFeatures();
    CHECK(std::set<std::string>(offered.begin(), offered.end()) == std::set<std::string>(ALL_FEATURES.begin(), ALL_FEATURES.end()));

    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(offered);
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith(ALL_FEATURES));
    clearSDK();
  }

  SUBCASE("unknown names are ignored with w402, the others are given") {
    Countly &countly = freshInstance();
    countly.disableHealthCheck();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS, "bogus"});
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(consentOf(requests[0]) == stateWith({"sessions"}));
    CHECK(logCode(countly, "w402") == 1);
    clearSDK();
  }

  SUBCASE("a second setConsentEnabled replaces the first") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS});
    countly.setConsentEnabled({cly::consent::EVENTS});
    start(countly);

    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(!requests.empty());
    CHECK(consentOf(requests[0]) == stateWith({"events"}));
    clearSDK();
  }

  SUBCASE("without the requirement, no consent state is sent") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setConsentEnabled({cly::consent::SESSIONS});
    start(countly);
    CHECK(takeRequests(countly).empty());
    clearSDK();
  }

  SUBCASE("after start, the consent options are ignored") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    start(countly);
    takeRequests(countly);

    countly.setConsentEnabled({cly::consent::EVENTS});
    countly.setRequiresConsent(false);
    CHECK_FALSE(countly.getConsent(cly::consent::EVENTS));
    CHECK(takeRequests(countly).empty());
    clearSDK();
  }
}

TEST_CASE("consent: a session start carries the empty location while location is off") {
  SUBCASE("the server turned location tracking off") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    std::string settings = json({{"lt", false}}).dump();
    countly.setSDKBehaviorSettings(settings);
    start(countly);
    takeRequests(countly);

    REQUIRE(countly.beginSession());
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "location") == "");
    CHECK(!has(requests[0], "country_code"));
    CHECK(!has(requests[0], "city"));
    CHECK(!has(requests[0], "ip_address"));
    clearSDK();
  }

  SUBCASE("while location is on, a session start carries no location parameter of its own") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    start(countly);
    takeRequests(countly);

    REQUIRE(countly.beginSession());
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(!has(requests[0], "location"));
    clearSDK();
  }

  SUBCASE("the location consent is missing") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS});
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(has(requests[0], "consent"));

    REQUIRE(countly.beginSession());
    requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "location") == "");
    clearSDK();
  }

  SUBCASE("with the location consent, the stored values go with the session start") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({cly::consent::SESSIONS, cly::consent::LOCATION});
    start(countly);
    takeRequests(countly);
    countly.setLocation("TR", "Istanbul", "", "203.0.113.7");
    takeRequests(countly);

    REQUIRE(countly.beginSession());
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 1);
    CHECK(valueOf(requests[0], "country_code") == "TR");
    CHECK(valueOf(requests[0], "city") == "Istanbul");
    CHECK(valueOf(requests[0], "ip_address") == "203.0.113.7");
    CHECK(!has(requests[0], "location"));
    clearSDK();
  }

  SUBCASE("required with nothing given and manual sessions, start ends with a location erase") {
    Countly &countly = freshInstance();
    countly.enableManualSessionControl();
    countly.setRequiresConsent(true);
    start(countly);
    std::vector<HTTPCall> requests = takeRequests(countly);
    REQUIRE(requests.size() == 2);
    CHECK(consentOf(requests[0]) == stateWith({}));
    CHECK(valueOf(requests[1], "location") == "");
    clearSDK();
  }
}

TEST_CASE("consent: each instance has its own consent state") {
  clearSDK();
  settings_server.reset();
  remove("test-consent-a.db");
  remove("test-consent-b.db");
  ThreadSafeHTTPCallQueue callsA;
  ThreadSafeHTTPCallQueue callsB;
  {
    Countly a;
    Countly b;
    a.setHTTPClient([&callsA](bool use_post, const std::string &url, const std::string &data) { return fakeSendHTTPInto(callsA, use_post, url, data); });
    b.setHTTPClient([&callsB](bool use_post, const std::string &url, const std::string &data) { return fakeSendHTTPInto(callsB, use_post, url, data); });
    a.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    b.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    a.SetPath("test-consent-a.db");
    b.SetPath("test-consent-b.db");
    a.enableManualSessionControl();
    b.enableManualSessionControl();
    a.disableSDKBehaviorSettingsUpdates();
    b.disableSDKBehaviorSettingsUpdates();
    a.setRequiresConsent(true);
    a.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    b.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    a.processRQDebug();
    b.processRQDebug();

    auto consentRequests = [](ThreadSafeHTTPCallQueue &calls) {
      size_t count = 0;
      for (size_t index = 0; index < calls.size(); index++) {
        if (has(calls.at(index), "consent")) {
          count++;
        }
      }
      return count;
    };
    CHECK(consentRequests(callsA) == 1);
    CHECK(consentRequests(callsB) == 0);

    a.giveConsent({cly::consent::VIEWS});
    CHECK_FALSE(a.getConsent(cly::consent::EVENTS));
    CHECK(b.getConsent(cly::consent::EVENTS));
    a.addEvent(Event("click", 1));
    b.addEvent(Event("click", 1));
    CHECK(a.checkEQSize() == 0);
    CHECK(b.checkEQSize() == 1);
  }
  remove("test-consent-a.db");
  remove("test-consent-b.db");
}

TEST_CASE("consent: checkAllConsent logs every feature and changes nothing") {
  Countly &countly = freshInstance();
  countly.setLogger(captureLog);
  countly.enableManualSessionControl();
  countly.setRequiresConsent(true);
  countly.setConsentEnabled({cly::consent::SESSIONS});
  start(countly);
  takeRequests(countly);

  clearCapturedLogs();
  countly.checkAllConsent();
  for (const std::string &feature : ALL_FEATURES) {
    CAPTURE(feature);
    CHECK(capturedLogContains(feature));
  }
  CHECK(takeRequests(countly).empty());
  CHECK(countly.getConsent(cly::consent::SESSIONS));
  clearSDK();
  clearCapturedLogs();
}
