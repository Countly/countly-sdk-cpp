#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "doctest.h"

#include "countly/health_check_module.hpp"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
/**
 * A storage that keeps only the health check state, for the counter tests.
 */
class HealthCheckStateStorage : public StorageModuleBase {
public:
  std::string state;

  HealthCheckStateStorage() : StorageModuleBase(nullptr, nullptr) {}
  void init() override {}
  long long RQCount() override { return 0; }
  void RQClearAll() override {}
  void RQRemoveFront() override {}
  const std::shared_ptr<DataEntry> RQPeekFront() override { return std::make_shared<DataEntry>(-1, ""); }
  std::vector<std::shared_ptr<DataEntry>> RQPeekAll() override { return {}; }
  void RQRemoveFront(std::shared_ptr<DataEntry> request) override { (void)request; }
  void RQInsertAtEnd(const std::string &request) override { (void)request; }
  void storeSDKBehaviorSettings(const std::string &sdk_behavior_settings) override { (void)sdk_behavior_settings; }
  std::string getSDKBehaviorSettings() override { return ""; }
  void storeHealthCheckState(const std::string &value) override { state = value; }
  std::string getHealthCheckState() override { return state; }
};

/**
 * The count of a method in a health check, 0 when it was not counted.
 */
long long usage(const json &hc, const std::string &area, const std::string &method) {
  if (!hc["fu"].contains(area) || !hc["fu"][area].contains(method)) {
    return 0;
  }
  return hc["fu"][area][method].get<long long>();
}

/**
 * The count of a log code in a health check, 0 when it was not recorded.
 */
long long code(const json &hc, const std::string &logCode) {
  if (!hc["lc"].contains(logCode)) {
    return 0;
  }
  return hc["lc"][logCode].get<long long>();
}

/**
 * Waits until the fake HTTP client received a health check, at most two seconds.
 */
bool waitForHealthCheck(size_t count = 1) {
  for (int attempt = 0; attempt < 200; attempt++) {
    if (health_check_call_queue.size() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

/**
 * Starts the default instance with the fake HTTP client and manual session control, so no session
 * request is queued unless a test makes one.
 */
void startWithHealthCheck(Countly &countly) {
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.setMetrics("Windows", "10", "PC", "1920x1080", "", "2.4.1");
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
}
} // namespace

TEST_CASE("health check counters") {
  SUBCASE("a new counter reports the defaults, with empty method usage and log codes") {
    HealthCheckModule module;
    json hc = module.snapshot();
    CHECK(hc["el"] == 0);
    CHECK(hc["wl"] == 0);
    CHECK(hc["sc"] == -1);
    CHECK(hc["em"] == "");
    CHECK(hc["bom"] == 0);
    CHECK(hc["cbom"] == 0);
    CHECK(hc["fu"].is_object());
    CHECK(hc["fu"].empty());
    CHECK(hc["lc"].is_object());
    CHECK(hc["lc"].empty());
  }

  SUBCASE("only the last refused request is kept, status codes outside 1..999 are ignored, the message is cut") {
    HealthCheckModule module;
    module.logFailedNetworkRequest(500, "first");
    module.logFailedNetworkRequest(0, "ignored");
    module.logFailedNetworkRequest(1000, "ignored");
    module.logFailedNetworkRequest(400, std::string(1200, 'x'));
    json hc = module.snapshot();
    CHECK(hc["sc"] == 400);
    CHECK(hc["em"].get<std::string>().size() == 1000);
  }

  SUBCASE("cbom keeps the longest run of backoffs, w320 counts every backoff and w321 every one that continues a run") {
    HealthCheckModule module;
    module.logBackoffRequest();
    module.logBackoffRequest();
    module.logBackoffRequest();
    module.saveState();
    module.logConsecutiveBackoffRequest();
    module.logBackoffRequest();
    module.logBackoffRequest();
    json hc = module.snapshot();
    CHECK(hc["bom"] == 5);
    CHECK(hc["cbom"] == 3);
    CHECK(code(hc, "w320") == 5);
    CHECK(code(hc, "w321") == 3);
  }

  SUBCASE("counts stop at 65535 and empty names are ignored") {
    HealthCheckModule module;
    module.recordLogCode("w101", 70000);
    module.recordLogCode("w101");
    module.recordUsage("events", "");
    module.recordUsage("", "record");
    json hc = module.snapshot();
    CHECK(code(hc, "w101") == 65535);
    CHECK(hc["fu"].empty());
  }

  SUBCASE("an accepted health check takes away only what it reported") {
    std::shared_ptr<HealthCheckStateStorage> storage = std::make_shared<HealthCheckStateStorage>();
    HealthCheckModule module;
    module.setStorage(storage);
    module.logWarning();
    module.logWarning();
    module.logFailedNetworkRequest(500, "down");
    module.logBackoffRequest();
    module.recordUsage("events", "record");
    module.recordLogCode("w720");

    json sent = module.snapshot();

    module.logWarning();
    module.logFailedNetworkRequest(400, "bad");
    module.logBackoffRequest();
    module.recordUsage("events", "record");
    module.recordUsage("views", "start");

    module.clearSentValues(sent);
    json left = module.snapshot();
    CHECK(left["wl"] == 1);
    CHECK(left["sc"] == 400);
    CHECK(left["em"] == "bad");
    CHECK(left["bom"] == 1);
    CHECK(left["cbom"] == 2);
    CHECK(usage(left, "events", "record") == 1);
    CHECK(usage(left, "views", "start") == 1);
    CHECK(code(left, "w720") == 0);
    CHECK(json::parse(storage->state)["LWar"] == 1);

    module.clearSentValues(module.snapshot());
    json empty = module.snapshot();
    CHECK(empty["sc"] == -1);
    CHECK(empty["em"] == "");
    CHECK(empty["cbom"] == 0);
    CHECK(empty["fu"].empty());
  }

  SUBCASE("a stored state is restored and added to what was counted before, with the storage keys of the guide") {
    std::shared_ptr<HealthCheckStateStorage> storage = std::make_shared<HealthCheckStateStorage>();
    storage->state = R"({"LErr":2,"LWar":3,"RStatC":503,"REMsg":"Service Unavailable","BReq":4,"CBReq":2,)"
                     R"("MU":{"events":{"record":5,"start":"x"},"views":7},"LC":{"w740":1,"w720":"y"}})";
    HealthCheckModule module;
    module.logWarning();
    module.setStorage(storage);

    json hc = module.snapshot();
    CHECK(hc["el"] == 2);
    CHECK(hc["wl"] == 4);
    CHECK(hc["sc"] == 503);
    CHECK(hc["em"] == "Service Unavailable");
    CHECK(hc["bom"] == 4);
    CHECK(hc["cbom"] == 2);
    CHECK(usage(hc, "events", "record") == 5);
    CHECK(usage(hc, "events", "start") == 0);
    CHECK(!hc["fu"].contains("views"));
    CHECK(code(hc, "w740") == 1);
    CHECK(!hc["lc"].contains("w720"));

    module.saveState();
    json stored = json::parse(storage->state);
    for (const char *key : {"LErr", "LWar", "RStatC", "REMsg", "BReq", "CBReq", "MU", "LC"}) {
      CHECK(stored.contains(key));
    }
  }

  SUBCASE("a stored state that is not valid JSON is cleared and reported as e503") {
    std::shared_ptr<HealthCheckStateStorage> storage = std::make_shared<HealthCheckStateStorage>();
    storage->state = "{not json";
    HealthCheckModule module;
    module.setStorage(storage);

    json hc = module.snapshot();
    CHECK(storage->state.empty());
    CHECK(hc["el"] == 0);
    CHECK(code(hc, "e503") == 1);
  }
}

TEST_CASE("health check requests") {
  SUBCASE("sent once after start as a request to /i, with the base parameters, the app version and the counters") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setEventsToRQThreshold(0);
    startWithHealthCheck(countly);

    REQUIRE(waitForHealthCheck());
    HTTPCall call = health_check_call_queue.front();
    CHECK(call.url == "/i");
    CHECK(call.data["app_key"] == COUNTLY_TEST_APP_KEY);
    CHECK(call.data["device_id"] == COUNTLY_TEST_DEVICE_ID);
    CHECK(!call.data["timestamp"].empty());
    CHECK(call.data["sdk_name"] == COUNTLY_SDK_NAME);
    CHECK(call.data["sdk_version"] == COUNTLY_SDK_VERSION);
    CHECK(json::parse(call.data["metrics"]) == json({{"_app_version", "2.4.1"}}));

    json hc = json::parse(call.data["hc"]);
    for (const char *key : {"el", "wl", "sc", "em", "bom", "cbom", "fu", "lc"}) {
      CHECK(hc.contains(key));
    }
    CHECK(hc["wl"].get<long long>() >= 1);
    CHECK(code(hc, "w121") == 1);

    // the fake client accepts it, so what it reported is taken away once the answer is handled
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(code(countly.debugHealthCheck()->snapshot(), "w121") == 0);

    countly.updateSession();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(health_check_call_queue.size() == 1);
    clearSDK();
  }

  SUBCASE("disabled, nothing is sent, but warnings are still counted") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.disableHealthCheck();
    startWithHealthCheck(countly);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(health_check_call_queue.empty());

    long long before = countly.debugHealthCheck()->snapshot()["wl"].get<long long>();
    countly.endSession();
    CHECK(countly.debugHealthCheck()->snapshot()["wl"].get<long long>() == before + 1);
    clearSDK();
  }

  SUBCASE("networking turned off by the SDK behavior settings, nothing is sent") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    std::string settings = json({{"networking", false}}).dump();
    countly.setSDKBehaviorSettings(settings);
    startWithHealthCheck(countly);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(health_check_call_queue.empty());
    clearSDK();
  }

  SUBCASE("a refused health check leaves the counters for the next start") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
      HTTPResponse response = fakeSendHTTP(use_post, url, data);
      if (data.find("hc=") != std::string::npos) {
        return HTTPResponse{false, json::object(), 400, R"({"result":"App does not exist"})"};
      }
      return response;
    });
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setEventsToRQThreshold(0);
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

    REQUIRE(waitForHealthCheck());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(code(countly.debugHealthCheck()->snapshot(), "w121") == 1);
    clearSDK();
  }
}

TEST_CASE("health check method usage and log codes") {
  SUBCASE("the app's session, event, view and crash calls are counted under the names of the guide") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    startWithHealthCheck(countly);
    REQUIRE(waitForHealthCheck());

    countly.beginSession();
    countly.updateSession();
    countly.endSession();

    countly.addEvent(Event("click", 1));
    countly.RecordEvent("purchase", 2);

    std::string viewId = countly.views().openView("first");
    countly.views().closeViewWithID(viewId);
    countly.views().openView("second");
    countly.views().closeViewWithName("second");

    countly.crash().addBreadcrumb("crumb");
    countly.crash().recordException("title", "trace", false, {{"_os", "Windows"}, {"_app_version", "2.4.1"}});

    json hc = countly.debugHealthCheck()->snapshot();
    CHECK(usage(hc, "sessions", "begin") == 1);
    CHECK(usage(hc, "sessions", "update") == 1);
    CHECK(usage(hc, "sessions", "end") == 1);
    CHECK(usage(hc, "events", "record") == 2);
    CHECK(usage(hc, "views", "start") == 2);
    CHECK(usage(hc, "views", "stop") == 2);
    CHECK(usage(hc, "crashes", "log") == 1);
    CHECK(usage(hc, "crashes", "record") == 1);
    clearSDK();
  }

  SUBCASE("sessions the SDK begins and ends by itself carry :a") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(test_utils::fakeSendHTTP);
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.disableSDKBehaviorSettingsUpdates();
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);
    REQUIRE(waitForHealthCheck());
    std::shared_ptr<HealthCheckModule> healthCheck = countly.debugHealthCheck();

    // the session begun at start is in the first health check, which the fake client accepts
    CHECK(usage(json::parse(health_check_call_queue.front().data["hc"]), "sessions", "begin:a") == 1);
    countly.stop();
    json hc = healthCheck->snapshot();
    CHECK(usage(hc, "sessions", "end:a") == 1);
    CHECK(usage(hc, "sessions", "begin") == 0);
    clearSDK();
  }

  SUBCASE("misuse of the session and view methods records the codes of the guide") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.addEvent(Event("too early", 1));
    startWithHealthCheck(countly);
    REQUIRE(waitForHealthCheck());

    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly.beginSession();
    countly.beginSession();
    countly.endSession();
    countly.endSession();
    countly.views().openView("");
    countly.views().closeViewWithID("unknown");
    countly.setCountry("TR");

    json sent = json::parse(health_check_call_queue.front().data["hc"]);
    CHECK(code(sent, "w101") == 1);

    json hc = countly.debugHealthCheck()->snapshot();
    CHECK(code(hc, "w110") == 1);
    CHECK(code(hc, "w760") == 2);
    CHECK(code(hc, "w710") == 1);
    CHECK(code(hc, "w730") == 1);
    CHECK(code(hc, "w120") == 1);
    clearSDK();
  }

  SUBCASE("a queued request the server refuses is reported in sc and em") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
      HTTPResponse response = fakeSendHTTP(use_post, url, data);
      if (data.find("hc=") == std::string::npos && url == "/i") {
        return HTTPResponse{false, json::object(), 400, R"({"result":"App does not exist"})"};
      }
      return response;
    });
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    REQUIRE(waitForHealthCheck());

    countly.beginSession();
    countly.processRQDebug();

    json hc = countly.debugHealthCheck()->snapshot();
    CHECK(hc["sc"] == 400);
    CHECK(hc["em"] == R"({"result":"App does not exist"})");
    clearSDK();
  }
}
