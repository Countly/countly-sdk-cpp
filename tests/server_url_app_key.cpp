#include "test_utils.hpp"

#include "doctest.h"
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace cly;
using namespace test_utils;

namespace {
const std::string FIRST_APP_KEY = COUNTLY_TEST_APP_KEY;
const std::string SECOND_APP_KEY = "b3c2d1e0f9a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4";
const std::string THIRD_APP_KEY = "c4d3e2f1a0b9c8d7e6f5a4b3c2d1e0f9a8b7c6d5";

/**
 * The requests the fake HTTP client received that carry every given parameter, in the order they were sent. An empty
 * value matches any value.
 */
std::vector<HTTPCall> callsWith(const std::map<std::string, std::string> &parameters) {
  std::vector<HTTPCall> calls;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    bool matches = true;
    for (const auto &parameter : parameters) {
      const auto found = call.data.find(parameter.first);
      matches = matches && found != call.data.end() && (parameter.second.empty() || found->second == parameter.second);
    }
    if (matches) {
      calls.push_back(call);
    }
  }
  return calls;
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
 * Waits up to five seconds until the fake HTTP client received 'count' requests carrying every given parameter.
 */
bool waitForRequest(const std::map<std::string, std::string> &parameters, size_t count = 1) {
  return waitUntil([&]() { return callsWith(parameters).size() >= count; });
}

/**
 * The requests sent to /i, each as what it is for and the app key it carries, such as "begin_session <app key>".
 */
std::vector<std::string> queuedRequestsSent() {
  std::vector<std::string> sent;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.url != "/i") {
      continue;
    }
    std::string kind = "other";
    // end_session comes before events because the end of a session carries no events, and begin_session before user_details
    for (const char *parameter : {"begin_session", "end_session", "events", "user_details", "consent", "location"}) {
      if (call.data.count(parameter) > 0) {
        kind = parameter;
        break;
      }
    }
    sent.push_back(kind + " " + call.data["app_key"]);
  }
  return sent;
}

/**
 * The keys of the events a request carries, in their order.
 */
std::vector<std::string> eventKeys(const HTTPCall &call) {
  std::vector<std::string> keys;
  for (const nlohmann::json &event : nlohmann::json::parse(call.data.at("events"))) {
    keys.push_back(event["key"].get<std::string>());
  }
  return keys;
}

/**
 * How often the health check of the instance counted the log code.
 */
long long logCodeCount(Countly &countly, const std::string &code) {
  const nlohmann::json codes = countly.debugHealthCheck()->snapshot().at("lc");
  return codes.contains(code) ? codes.at(code).get<long long>() : 0;
}

/**
 * Gets the default instance ready for start(): the HTTP client, the test device ID and database, a captured log, and
 * an update loop that sleeps through the test, so that only the test sends the request queue.
 */
Countly &prepareCountly(HTTPClientFunction client = fakeSendHTTP) {
  clearSDK();
  clearCapturedLogs();
  Countly &countly = Countly::getInstance();
  countly.setLogger(captureLog);
  countly.setHTTPClient(client);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(60000);
  return countly;
}

/**
 * Starts the instance, sends what start() queued and forgets every request sent so far.
 */
void startAndForget(Countly &countly) {
  countly.start(FIRST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(waitForRequest({{"method", "sc"}}));
  // The fetches of start() run on threads of their own, a remote config download with the first session included
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  countly.processRQDebug();
  http_call_queue.clear();
}

/**
 * Holds back the first request whose data contains a marker until the test releases it, so that the test can act
 * while that request is on its way.
 */
class RequestHold {
public:
  /**
   * Holds back the next request whose data contains 'marker'.
   */
  void holdNext(const std::string &marker) {
    std::lock_guard<std::mutex> lock(_mutex);
    _marker = marker;
    _held = false;
    _released = false;
  }

  /**
   * Waits up to five seconds until a request is held back.
   */
  bool waitUntilHeld() {
    std::unique_lock<std::mutex> lock(_mutex);
    return _changed.wait_for(lock, std::chrono::seconds(5), [this]() { return _held; });
  }

  /**
   * Lets the held request and every later one through.
   */
  void release() {
    {
      std::lock_guard<std::mutex> lock(_mutex);
      _released = true;
    }
    _changed.notify_all();
  }

  /**
   * HTTP client that holds back the marked request, then answers every request as fakeSendHTTP does.
   */
  HTTPResponse send(bool use_post, const std::string &url, const std::string &data) {
    {
      std::unique_lock<std::mutex> lock(_mutex);
      if (!_released && !_held && data.find(_marker) != std::string::npos) {
        _held = true;
        _changed.notify_all();
        _changed.wait(lock, [this]() { return _released; });
      }
    }
    return fakeSendHTTP(use_post, url, data);
  }

private:
  std::mutex _mutex;
  std::condition_variable _changed;
  std::string _marker;
  bool _held = false;
  bool _released = true;
};

RequestHold request_hold;

HTTPResponse sendThroughHold(bool use_post, const std::string &url, const std::string &data) { return request_hold.send(use_post, url, data); }

/**
 * Lets the held request through and waits for the thread that sends it, at the latest when the test ends, also when
 * it ends early, so that no thread stays blocked.
 */
struct HoldScope {
  std::thread sender;

  void release() {
    request_hold.release();
    if (sender.joinable()) {
      sender.join();
    }
  }

  ~HoldScope() { release(); }
};

/**
 * HTTP client that holds back the marked request like sendThroughHold, and answers a download of every remote config
 * value for the second app key with that app's value.
 */
HTTPResponse sendThroughHoldWithSecondAppConfig(bool use_post, const std::string &url, const std::string &data) {
  HTTPResponse response = request_hold.send(use_post, url, data);
  if (url == "/o/sdk" && data.find("method=rc") != std::string::npos && data.find("app_key=" + SECOND_APP_KEY) != std::string::npos && data.find("keys=") == std::string::npos) {
    response.success = true;
    response.data = {{"color", "#00FF00"}};
  }
  return response;
}

std::mutex raw_requests_mutex;
std::vector<std::string> raw_requests;

/**
 * HTTP client that keeps the data of every queued request as it was sent, then answers as fakeSendHTTP does.
 */
HTTPResponse recordRawRequest(bool use_post, const std::string &url, const std::string &data) {
  if (url == "/i" && data.find("hc=") == std::string::npos) {
    std::lock_guard<std::mutex> lock(raw_requests_mutex);
    raw_requests.push_back(data);
  }
  return fakeSendHTTP(use_post, url, data);
}
} // namespace

TEST_CASE("Changing the server URL") {
  SUBCASE("a valid URL is taken with the port the way start() takes them, and the app key stays") {
    Countly &countly = prepareCountly();
    startAndForget(countly);
    const long long out_of_range_ports = logCodeCount(countly, "w121");

    countly.changeServerUrl("https://changed.countly.notarealdomain//");
    CHECK(countly.getConfiguration().serverUrl == "https://changed.countly.notarealdomain");
    CHECK(countly.getConfiguration().port == 443);

    countly.changeServerUrl("http://other.countly.notarealdomain/", 9000);
    CHECK(countly.getConfiguration().serverUrl == "http://other.countly.notarealdomain");
    CHECK(countly.getConfiguration().port == 9000);

    countly.changeServerUrl("http://other.countly.notarealdomain", 70000);
    CHECK(countly.getConfiguration().port == 80);
    CHECK(logCodeCount(countly, "w121") == out_of_range_ports + 1);

    CHECK(countly.getConfiguration().appKey == FIRST_APP_KEY);
    CHECK(capturedLogContains("changeServerUrl, Changing the server URL to [http://other.countly.notarealdomain/], port = [9000]"));
  }

  SUBCASE("an invalid URL is ignored and logged, and the current one stays") {
    Countly &countly = prepareCountly();
    startAndForget(countly);
    const long long invalid_urls = logCodeCount(countly, "e102");

    const std::vector<std::string> urls = {"", "not a url", "ftp://x", "http://", "http:///", "https://:443", "changed.countly.notarealdomain"};
    for (const std::string &url : urls) {
      countly.changeServerUrl(url);
      CHECK(countly.getConfiguration().serverUrl == COUNTLY_TEST_HOST);
      CHECK(countly.getConfiguration().port == COUNTLY_TEST_PORT);
    }
    CHECK(logCodeCount(countly, "e102") == invalid_urls + static_cast<long long>(urls.size()));
    CHECK(capturedLogContains("changeServerUrl, The server URL [ftp://x] is not an absolute http:// or https:// URL, the current server URL stays."));

    // Nothing is queued or fetched for an ignored URL
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    countly.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("a change is followed by the consent state, the SDK behavior settings fetch and the remote config download, with the current app key") {
    Countly &countly = prepareCountly();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({consent::SESSIONS, consent::REMOTE_CONFIG});
    countly.enableRemoteConfig();
    startAndForget(countly);

    countly.changeServerUrl("https://changed.countly.notarealdomain");
    REQUIRE(waitForRequest({{"method", "sc"}}));
    REQUIRE(waitForRequest({{"method", "rc"}}));
    countly.processRQDebug();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"consent " + FIRST_APP_KEY});
    const nlohmann::json state = nlohmann::json::parse(callsWith({{"consent", ""}}).at(0).data["consent"]);
    CHECK(state["sessions"] == true);
    CHECK(state["remote-config"] == true);
    CHECK(state["events"] == false);
    for (const char *method : {"sc", "rc"}) {
      const HTTPCall fetch = callsWith({{"method", method}}).at(0);
      CHECK(fetch.data.at("app_key") == FIRST_APP_KEY);
      CHECK(fetch.data.at("device_id") == COUNTLY_TEST_DEVICE_ID);
    }
  }

  SUBCASE("a call before start is ignored") {
    Countly &countly = prepareCountly();
    countly.changeServerUrl("https://changed.countly.notarealdomain");
    CHECK(countly.getConfiguration().serverUrl.empty());
    CHECK(logCodeCount(countly, "w101") == 1);
    CHECK(capturedLogContains("changeServerUrl, SDK is not initialized."));
  }
}

TEST_CASE("Changing the app key") {
  SUBCASE("the old app gets its views, events and session end, the new app a new session, and queued requests keep their key") {
    Countly &countly = prepareCountly();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled(consent::allFeatures());
    startAndForget(countly);

    countly.addEvent(cly::Event("queued_before", 1));
    countly.flushEvents();
    countly.views().openView("Home");
    countly.addEvent(cly::Event("recorded_before", 1));
    countly.changeAppKey(SECOND_APP_KEY);
    countly.addEvent(cly::Event("recorded_after", 1));
    countly.flushEvents();
    REQUIRE(waitForRequest({{"method", "sc"}}));
    countly.processRQDebug();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"events " + FIRST_APP_KEY, "events " + FIRST_APP_KEY, "end_session " + FIRST_APP_KEY, "consent " + SECOND_APP_KEY, "begin_session " + SECOND_APP_KEY, "events " + SECOND_APP_KEY});
    const std::vector<HTTPCall> events = callsWith({{"events", ""}});
    REQUIRE(events.size() == 3);
    CHECK(eventKeys(events[0]) == std::vector<std::string>{"queued_before"});
    CHECK(eventKeys(events[1]) == std::vector<std::string>{"[CLY]_view", "recorded_before", "[CLY]_view"});
    CHECK(eventKeys(events[2]) == std::vector<std::string>{"recorded_after"});
    CHECK(callsWith({{"method", "sc"}}).at(0).data["app_key"] == SECOND_APP_KEY);
    CHECK(callsWith({{"device_id", COUNTLY_TEST_DEVICE_ID}}).size() == http_call_queue.size());
    CHECK(countly.getConfiguration().appKey == SECOND_APP_KEY);
  }

  SUBCASE("an invalid key and the current key are ignored") {
    Countly &countly = prepareCountly();
    startAndForget(countly);
    const long long invalid_keys = logCodeCount(countly, "e103");

    countly.changeAppKey("");
    countly.changeAppKey("YOUR_APP_KEY");
    countly.changeAppKey(FIRST_APP_KEY);

    CHECK(logCodeCount(countly, "e103") == invalid_keys + 2);
    CHECK(capturedLogContains("changeAppKey, The app key can't be empty or the placeholder \"YOUR_APP_KEY\", the current app key stays."));
    CHECK(capturedLogContains("changeAppKey, The new app key is the current one, the call is ignored."));
    CHECK(countly.getConfiguration().appKey == FIRST_APP_KEY);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    countly.processRQDebug();
    CHECK(http_call_queue.empty());
  }

  SUBCASE("with manual sessions, user details still waiting go to the old app, and only a running session begins again") {
    Countly &countly = prepareCountly();
    countly.enableManualSessionControl();
    countly.setUserDetails({{"name", "Waiting User"}});
    startAndForget(countly);

    countly.changeAppKey(SECOND_APP_KEY);
    countly.beginSession();
    countly.changeAppKey(THIRD_APP_KEY);
    REQUIRE(waitForRequest({{"method", "sc"}, {"app_key", THIRD_APP_KEY}}));
    countly.processRQDebug();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"user_details " + FIRST_APP_KEY, "begin_session " + SECOND_APP_KEY, "end_session " + SECOND_APP_KEY, "begin_session " + THIRD_APP_KEY});
    CHECK(nlohmann::json::parse(callsWith({{"user_details", ""}}).at(0).data["user_details"]) == nlohmann::json({{"name", "Waiting User"}}));
  }

  SUBCASE("the remote config values of the old app are cleared and the new app's are downloaded once") {
    Countly &countly = prepareCountly();
    countly.enableRemoteConfig();
    startAndForget(countly);
    std::string keys[] = {"color"};
    countly.updateRemoteConfigFor(keys, 1);
    REQUIRE(waitForRequest({{"keys", "[\"color\"]"}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(countly.getRemoteConfigValue("color") == "#FF9900");

    countly.changeAppKey(SECOND_APP_KEY);
    CHECK(countly.getRemoteConfigValue("color").is_null());
    CHECK(waitForRequest({{"method", "rc"}, {"app_key", SECOND_APP_KEY}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(callsWith({{"method", "rc"}, {"app_key", SECOND_APP_KEY}}).size() == 1);
  }

  SUBCASE("a call before start is ignored") {
    Countly &countly = prepareCountly();
    countly.changeAppKey(SECOND_APP_KEY);
    CHECK(countly.getConfiguration().appKey.empty());
    CHECK(logCodeCount(countly, "w101") == 1);
    CHECK(capturedLogContains("changeAppKey, SDK is not initialized."));
  }
}

TEST_CASE("Queue operations") {
  SUBCASE("flushQueues clears both queues, and what is not queued yet stays") {
    Countly &countly = prepareCountly();
    countly.enableManualSessionControl();
    countly.setUserDetails({{"name", "Waiting User"}});
    startAndForget(countly);
    countly.addEvent(cly::Event("queued_event", 1));
    countly.flushEvents();
    countly.views().openView("Home");
    countly.addEvent(cly::Event("flushed_event", 1));
    countly.setLocation("TR", "Izmir", "38.42,27.14", "");
    REQUIRE(countly.checkRQSize() == 1);
    REQUIRE(countly.checkEQSize() == 2);

    countly.flushQueues();
    CHECK(countly.checkRQSize() == 0);
    CHECK(countly.checkEQSize() == 0);

    countly.views().closeViewWithName("Home");
    countly.beginSession();
    countly.attemptToSendStoredRequests();
    CHECK(queuedRequestsSent() == std::vector<std::string>{"begin_session " + FIRST_APP_KEY, "events " + FIRST_APP_KEY});
    const HTTPCall begin = callsWith({{"begin_session", ""}}).at(0);
    CHECK(nlohmann::json::parse(begin.data.at("user_details")) == nlohmann::json({{"name", "Waiting User"}}));
    CHECK(begin.data.at("city") == "Izmir");
    CHECK(eventKeys(callsWith({{"events", ""}}).at(0)) == std::vector<std::string>{"[CLY]_view"});
  }

  SUBCASE("replaceAllAppKeysInQueueWithCurrentAppKey sends the queued requests to the new app with a valid checksum") {
    Countly &countly = prepareCountly(recordRawRequest);
    countly.setSalt("test-salt");
    startAndForget(countly);
    {
      std::lock_guard<std::mutex> lock(raw_requests_mutex);
      raw_requests.clear();
    }

    countly.addEvent(cly::Event("queued_before", 1));
    countly.flushEvents();
    countly.changeAppKey(SECOND_APP_KEY);
    countly.replaceAllAppKeysInQueueWithCurrentAppKey();
    countly.processRQDebug();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"events " + SECOND_APP_KEY, "end_session " + SECOND_APP_KEY, "begin_session " + SECOND_APP_KEY});
    CHECK(capturedLogContains("replaceAllAppKeysInQueueWithCurrentAppKey, Giving every queued request the current app key."));
    std::lock_guard<std::mutex> lock(raw_requests_mutex);
    REQUIRE(raw_requests.size() == 3);
    for (const std::string &raw_request : raw_requests) {
      const std::string::size_type checksum_start = raw_request.find("&checksum256=");
      REQUIRE(checksum_start != std::string::npos);
      CHECK(raw_request.find("app_key=" + SECOND_APP_KEY) != std::string::npos);
      CHECK(countly.calculateChecksum("test-salt", raw_request.substr(0, checksum_start)) == raw_request.substr(checksum_start + 13));
    }
  }

  SUBCASE("removeDifferentAppKeysFromQueue drops the queued requests of the old app") {
    Countly &countly = prepareCountly();
    startAndForget(countly);
    countly.addEvent(cly::Event("queued_before", 1));
    countly.flushEvents();
    countly.changeAppKey(SECOND_APP_KEY);
    countly.removeDifferentAppKeysFromQueue();
    countly.processRQDebug();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"begin_session " + SECOND_APP_KEY});
    CHECK(capturedLogContains("removeDifferentAppKeysFromQueue, Removed [2] queued requests with another app key."));
  }

  SUBCASE("attemptToSendStoredRequests sends the queued events and requests at once") {
    Countly &countly = prepareCountly();
    startAndForget(countly);
    countly.setLocation("TR", "Izmir", "38.42,27.14", "");
    countly.addEvent(cly::Event("sent_at_once", 1));
    countly.attemptToSendStoredRequests();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"location " + FIRST_APP_KEY, "events " + FIRST_APP_KEY});
    CHECK(eventKeys(callsWith({{"events", ""}}).at(0)) == std::vector<std::string>{"sent_at_once"});
    CHECK(countly.checkRQSize() == 0);
    CHECK(countly.checkEQSize() == 0);
  }

  SUBCASE("a call before start is ignored") {
    Countly &countly = prepareCountly();
    countly.flushQueues();
    countly.replaceAllAppKeysInQueueWithCurrentAppKey();
    countly.removeDifferentAppKeysFromQueue();
    countly.attemptToSendStoredRequests();
    CHECK(logCodeCount(countly, "w101") == 4);
    CHECK(capturedLogContains("flushQueues, SDK is not initialized."));
    CHECK(capturedLogContains("attemptToSendStoredRequests, SDK is not initialized."));
  }
}

TEST_CASE("Queue operations while a request is on its way") {
  HoldScope hold;
  request_hold.holdNext("events=");
  Countly &countly = prepareCountly(sendThroughHold);
  countly.enableManualSessionControl();
  startAndForget(countly);
  countly.addEvent(cly::Event("on_its_way", 1));
  hold.sender = std::thread([&countly]() { countly.attemptToSendStoredRequests(); });
  CHECK(request_hold.waitUntilHeld());

  SUBCASE("flushQueues: the request on its way is delivered, and the request queued next is not lost") {
    countly.flushQueues();
    // The queue is empty now, so this request takes the id of the one on its way
    countly.beginSession();
    hold.release();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"events " + FIRST_APP_KEY, "begin_session " + FIRST_APP_KEY});
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("removeDifferentAppKeysFromQueue: the request queued next is not lost") {
    countly.changeAppKey(SECOND_APP_KEY);
    countly.removeDifferentAppKeysFromQueue();
    countly.beginSession();
    hold.release();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"events " + FIRST_APP_KEY, "begin_session " + SECOND_APP_KEY});
    CHECK(capturedLogContains("removeDifferentAppKeysFromQueue, Removed [1] queued requests with another app key."));
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("replaceAllAppKeysInQueueWithCurrentAppKey: the request on its way is delivered once, with the old key") {
    countly.changeAppKey(SECOND_APP_KEY);
    countly.replaceAllAppKeysInQueueWithCurrentAppKey();
    hold.release();

    CHECK(queuedRequestsSent() == std::vector<std::string>{"events " + FIRST_APP_KEY});
    CHECK(countly.checkRQSize() == 0);
  }
}

TEST_CASE("After an app key change the new app's remote config is downloaded, and a download for the old key stores nothing") {
  HoldScope hold;
  // the download at the end of start() goes through, so the held one is the download for some keys
  request_hold.holdNext("keys=");
  Countly &countly = prepareCountly(sendThroughHoldWithSecondAppConfig);
  countly.enableManualSessionControl();
  countly.enableRemoteConfig();
  startAndForget(countly);
  std::string keys[] = {"color"};
  countly.updateRemoteConfigFor(keys, 1);
  REQUIRE(request_hold.waitUntilHeld());

  countly.changeAppKey(SECOND_APP_KEY);
  // The download for the new key does not wait for the one still on its way for the old key
  CHECK(waitForRequest({{"method", "rc"}, {"app_key", SECOND_APP_KEY}}));
  CHECK(waitUntil([&countly]() { return countly.getRemoteConfigValue("color") == "#00FF00"; }));

  hold.release();
  REQUIRE(waitForRequest({{"keys", "[\"color\"]"}}));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(countly.getRemoteConfigValue("color") == "#00FF00");

  // Neither download keeps the next one from starting
  countly.updateRemoteConfig();
  CHECK(waitForRequest({{"method", "rc"}, {"app_key", SECOND_APP_KEY}}, 2));
}
