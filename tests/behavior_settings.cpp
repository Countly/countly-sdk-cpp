#include <chrono>
#include <functional>
#include <mutex>
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
 * Answers the behavior settings fetches with the queued answers, one per fetch, and fails a fetch when none is left.
 */
struct SettingsAnswers {
  std::mutex mutex;
  std::vector<json> answers;
  std::vector<std::string> fetchedFor;

  void reset(std::vector<json> next) {
    std::lock_guard<std::mutex> lock(mutex);
    answers = std::move(next);
    fetchedFor.clear();
  }

  size_t fetches() {
    std::lock_guard<std::mutex> lock(mutex);
    return fetchedFor.size();
  }

  std::vector<std::string> devices() {
    std::lock_guard<std::mutex> lock(mutex);
    return fetchedFor;
  }
};

SettingsAnswers settings_answers;

HTTPResponse settingsClient(bool use_post, const std::string &url, const std::string &data) {
  HTTPResponse response = fakeSendHTTP(use_post, url, data);
  if (url != "/o/sdk" || data.find("method=sc") == std::string::npos) {
    return response;
  }
  std::string device_id;
  const std::string::size_type start = data.find("device_id=");
  if (start != std::string::npos) {
    device_id = data.substr(start + 10, data.find('&', start) - start - 10);
    decodeURL(device_id);
  }
  std::lock_guard<std::mutex> lock(settings_answers.mutex);
  settings_answers.fetchedFor.push_back(device_id);
  if (settings_answers.answers.empty()) {
    return response;
  }
  json answer = settings_answers.answers.front();
  settings_answers.answers.erase(settings_answers.answers.begin());
  return HTTPResponse{true, answer, 200, answer.dump()};
}

bool waitFor(std::function<bool()> condition) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return condition();
}

long long settingsLogCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

Countly &prepareSettings(const std::string &provided = "", bool fetchUpdates = true) {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(settingsClient);
  countly.disableHealthCheck();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  if (!provided.empty()) {
    countly.setSDKBehaviorSettings(provided);
  }
  if (!fetchUpdates) {
    countly.disableSDKBehaviorSettingsUpdates();
  }
  return countly;
}

void startSettings(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }

/**
 * Records the event, sends it if it was recorded, and tells whether the server received it.
 */
bool recorded(Countly &countly, const std::string &key) {
  countly.RecordEvent(key, 1);
  countly.flushEvents();
  countly.processRQDebug();
  return httpQueueContainsEvent(key);
}
} // namespace

TEST_CASE("A setting missing from a new answer goes back to its default") {
  Countly &countly = prepareSettings();
  settings_answers.reset({json({{"v", 2}, {"t", 1}, {"c", {{"tracking", false}}}}), json({{"v", 2}, {"t", 2}, {"c", json::object()}})});
  startSettings(countly);
  REQUIRE(waitFor([&countly] {
    countly.RecordEvent("probe", 1);
    return settingsLogCode(countly, "w505") > 0;
  }));

  // the device ID change fetches the settings again, and that answer has no "tracking"
  countly.setDeviceID("second-user", false);
  REQUIRE(waitFor([] { return settings_answers.fetches() == 2; }));
  CHECK(settings_answers.devices() == std::vector<std::string>{COUNTLY_TEST_DEVICE_ID, "second-user"});
  CHECK(waitFor([&countly] { return recorded(countly, "after_reset"); }));
}

TEST_CASE("After a device ID change with merge the settings are fetched once the merge request was sent") {
  Countly &countly = prepareSettings();
  settings_answers.reset({});
  startSettings(countly);
  REQUIRE(waitFor([] { return settings_answers.fetches() == 1; }));

  countly.setDeviceID("merged-user", true);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(settings_answers.fetches() == 1);

  countly.processRQDebug();
  REQUIRE(waitFor([] { return settings_answers.fetches() == 2; }));
  CHECK(settings_answers.devices()[1] == "merged-user");
  bool mergeBeforeFetch = false;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("old_device_id")) {
      mergeBeforeFetch = true;
    }
    if (call.data["method"] == "sc" && call.data["device_id"] == "merged-user") {
      break;
    }
  }
  CHECK(mergeBeforeFetch);
}

TEST_CASE("An empty filter list is ignored") {
  SUBCASE("an empty blacklist lets the whitelist apply") {
    Countly &countly = prepareSettings("{\"eb\":[],\"ew\":[\"allowed\"]}", false);
    startSettings(countly);
    CHECK(recorded(countly, "allowed"));
    CHECK_FALSE(recorded(countly, "not_listed"));
  }

  SUBCASE("a blacklist wins when both lists have entries") {
    Countly &countly = prepareSettings("{\"eb\":[\"blocked\"],\"ew\":[\"allowed\"]}", false);
    startSettings(countly);
    CHECK_FALSE(recorded(countly, "blocked"));
    CHECK(recorded(countly, "not_listed"));
  }
}

TEST_CASE("Provided settings") {
  SUBCASE("are accepted in the format of a server answer") {
    Countly &countly = prepareSettings("{\"v\":2,\"t\":1742459739383,\"c\":{\"cet\":false}}", false);
    startSettings(countly);
    CHECK_FALSE(recorded(countly, "custom_event"));
  }

  SUBCASE("are accepted as the settings object alone") {
    Countly &countly = prepareSettings("{\"cet\":false}", false);
    startSettings(countly);
    CHECK_FALSE(recorded(countly, "custom_event"));
  }

  SUBCASE("that are not JSON are ignored with a warning") {
    Countly &countly = prepareSettings("not json", false);
    startSettings(countly);
    CHECK(recorded(countly, "custom_event"));
  }

#ifdef COUNTLY_USE_SQLITE
  SUBCASE("are not kept for a later start") {
    Countly *countly = &prepareSettings("{\"cet\":false}", false);
    startSettings(*countly);
    CHECK_FALSE(recorded(*countly, "first_run"));

    Countly::halt();
    http_call_queue.clear();
    countly = &Countly::getInstance();
    countly->setHTTPClient(settingsClient);
    countly->disableHealthCheck();
    countly->disableSDKBehaviorSettingsUpdates();
    countly->enableManualSessionControl();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(TEST_DATABASE_NAME);
    startSettings(*countly);
    CHECK(recorded(*countly, "second_run"));
  }
#endif
}

TEST_CASE("disableSDKBehaviorSettingsUpdates sends no settings fetch at all") {
  Countly &countly = prepareSettings("", false);
  settings_answers.reset({});
  startSettings(countly);
  countly.setDeviceID("second-user", false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(settings_answers.fetches() == 0);
}

TEST_CASE("Server settings with an invalid value are ignored with a warning") {
  Countly &countly = prepareSettings("{\"rqs\":0,\"eqs\":\"ten\",\"bom_rqp\":2,\"tracking\":\"yes\",\"lkl\":-5}", false);
  startSettings(countly);
  CHECK(settingsLogCode(countly, "w121") == 5);
  CHECK(recorded(countly, "still_tracked"));
}

namespace {
/**
 * Prepares the default instance like prepareSettings(), with automatic sessions unless 'manual' is set.
 */
Countly &prepareSessionSettings(const std::string &provided, bool manual) {
  clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(settingsClient);
  countly.disableHealthCheck();
  if (manual) {
    countly.enableManualSessionControl();
  }
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  if (!provided.empty()) {
    countly.setSDKBehaviorSettings(provided);
  }
  return countly;
}

/**
 * The session requests the fake server received, in order: "begin", "update" or "end".
 */
std::vector<std::string> sessionRequests() {
  std::vector<std::string> kinds;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("begin_session")) {
      kinds.push_back("begin");
    } else if (call.data.count("end_session")) {
      kinds.push_back("end");
    } else if (call.data.count("session_duration")) {
      kinds.push_back("update");
    }
  }
  return kinds;
}

/**
 * A behavior settings answer of the server with the given settings.
 */
json settingsAnswer(const json &settings) { return json({{"v", 2}, {"t", 1}, {"c", settings}}); }
} // namespace

TEST_CASE("The server's automatic session tracking setting") {
  SUBCASE("ast false: no session begins by itself, and the app's session calls work") {
    Countly &countly = prepareSessionSettings("{\"ast\":false}", false);
    settings_answers.reset({});
    startSettings(countly);
    countly.processRQDebug();
    CHECK(sessionRequests().empty());

    CHECK(countly.beginSession());
    countly.processRQDebug();
    CHECK(sessionRequests() == std::vector<std::string>{"begin"});
  }

  SUBCASE("ast true: the SDK tracks sessions also when the app chose manual session control") {
    Countly &countly = prepareSessionSettings("{\"ast\":true}", true);
    settings_answers.reset({});
    startSettings(countly);

    CHECK_FALSE(countly.beginSession());
    countly.processRQDebug();
    CHECK(sessionRequests() == std::vector<std::string>{"begin"});
    CHECK(settingsLogCode(countly, "w740") == 1);
  }

  SUBCASE("ast false from the server ends the session the SDK runs") {
    Countly &countly = prepareSessionSettings("", false);
    settings_answers.reset({settingsAnswer(json::object()), settingsAnswer({{"ast", false}})});
    startSettings(countly);
    REQUIRE(waitFor([] { return settings_answers.fetches() == 1; }));
    countly.changeServerUrl(COUNTLY_TEST_HOST, COUNTLY_TEST_PORT);

    CHECK(waitFor([&countly] {
      countly.processRQDebug();
      return sessionRequests() == std::vector<std::string>{"begin", "end"};
    }));
    CHECK(countly.beginSession());
  }

  SUBCASE("ast true from the server begins a session for an app with manual session control") {
    Countly &countly = prepareSessionSettings("", true);
    settings_answers.reset({settingsAnswer(json::object()), settingsAnswer({{"ast", true}})});
    startSettings(countly);
    REQUIRE(waitFor([] { return settings_answers.fetches() == 1; }));
    countly.processRQDebug();
    REQUIRE(sessionRequests().empty());
    countly.changeServerUrl(COUNTLY_TEST_HOST, COUNTLY_TEST_PORT);

    CHECK(waitFor([&countly] {
      countly.processRQDebug();
      return sessionRequests() == std::vector<std::string>{"begin"};
    }));
  }
}
