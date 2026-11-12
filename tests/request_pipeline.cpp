#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

#ifdef COUNTLY_USE_SQLITE
#include "sqlite3.h"
#endif

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
/**
 * A request the scripted server received: whether it came as POST, its path, its parameters and its raw body.
 */
struct ReceivedRequest {
  bool post;
  std::string path;
  std::map<std::string, std::string> params;
  std::string raw;
};

/**
 * A fake Countly server whose answers a test can script. By default it answers every request like the server does.
 */
struct ScriptedServer {
  std::mutex mtx;
  std::vector<ReceivedRequest> received;
  // Called for every request; returns the answer. Replaced by tests that need another answer.
  std::function<HTTPResponse(const ReceivedRequest &)> answer;

  ScriptedServer() { reset(); }

  void reset() {
    std::lock_guard<std::mutex> lock(mtx);
    received.clear();
    answer = [](const ReceivedRequest &request) {
      HTTPResponse response{true, json({{"result", "Success"}}), 200};
      if (request.path == "/o/sdk") {
        response.success = false;
        response.status_code = 0;
      }
      return response;
    };
  }

  /**
   * Splits a request body into its decoded parameters.
   */
  static std::map<std::string, std::string> parse(const std::string &data) {
    std::map<std::string, std::string> params;
    std::string::size_type start = 0;
    while (start < data.size()) {
      std::string::size_type end = data.find('&', start);
      if (end == std::string::npos) {
        end = data.size();
      }
      const std::string pair = data.substr(start, end - start);
      const std::string::size_type equals = pair.find('=');
      std::string value = equals == std::string::npos ? std::string() : pair.substr(equals + 1);
      decodeURL(value);
      params[pair.substr(0, equals)] = value;
      start = end + 1;
    }
    return params;
  }

  HTTPResponse handle(bool post, const std::string &path, const std::string &data) {
    ReceivedRequest request{post, path, parse(data), data};
    std::function<HTTPResponse(const ReceivedRequest &)> respond;
    {
      std::lock_guard<std::mutex> lock(mtx);
      received.push_back(request);
      respond = answer;
    }
    return respond(request);
  }

  std::vector<ReceivedRequest> queued() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<ReceivedRequest> result;
    for (const ReceivedRequest &request : received) {
      if (request.path == "/i" && !request.params.count("hc")) {
        result.push_back(request);
      }
    }
    return result;
  }

  void setAnswer(std::function<HTTPResponse(const ReceivedRequest &)> respond) {
    std::lock_guard<std::mutex> lock(mtx);
    answer = std::move(respond);
  }
};

ScriptedServer server;

HTTPResponse scriptedClient(bool post, const std::string &path, const std::string &data) { return server.handle(post, path, data); }

std::string fakeSha(const std::string &data) { return "sha:" + std::to_string(std::hash<std::string>()(data)); }

/**
 * Prepares the default instance with the scripted server, manual sessions and no health check request.
 */
Countly &prepare() {
  clearSDK();
  server.reset();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(scriptedClient);
  countly.setSha256(fakeSha);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  // The update loop would send the queue every 3 seconds; the tests send it themselves
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  return countly;
}

void start(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false); }

long long logCode(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

/**
 * Polls until the condition holds or the timeout passes, calling the step between checks.
 */
bool eventually(std::function<bool()> condition, std::chrono::milliseconds timeout, std::function<void()> step = [] {}) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    step();
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return condition();
}

HTTPResponse failingAnswer(const ReceivedRequest &) { return HTTPResponse{false, json(), 503, "Service Unavailable"}; }

#ifdef COUNTLY_USE_SQLITE
/**
 * Stores a request with the given timestamp in the database, the way an earlier run would have left it.
 */
void storeRequest(const std::string &body) {
  sqlite3 *database = nullptr;
  REQUIRE(sqlite3_open(TEST_DATABASE_NAME, &database) == SQLITE_OK);
  REQUIRE(sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS Requests (RequestID INTEGER PRIMARY KEY, RequestData TEXT)", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_stmt *statement = nullptr;
  REQUIRE(sqlite3_prepare_v2(database, "INSERT INTO Requests (RequestData) VALUES(?);", -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, body.c_str(), -1, SQLITE_TRANSIENT);
  CHECK(sqlite3_step(statement) == SQLITE_DONE);
  sqlite3_finalize(statement);
  sqlite3_close(database);
}
#endif
} // namespace

TEST_CASE("Every request is sent as POST with its data in the body") {
  Countly &countly = prepare();
  countly.enableRemoteConfig();
  start(countly);
  countly.beginSession();
  countly.RecordEvent("purchase", 1);
  countly.flushEvents();
  countly.updateRemoteConfig();
  countly.processRQDebug();

  REQUIRE(eventually([] { return server.queued().size() >= 2; }, std::chrono::seconds(5)));
  REQUIRE(eventually(
      [] {
        std::lock_guard<std::mutex> lock(server.mtx);
        for (const ReceivedRequest &request : server.received) {
          if (request.path == "/o/sdk") {
            return true;
          }
        }
        return false;
      },
      std::chrono::seconds(5)));
  std::lock_guard<std::mutex> lock(server.mtx);
  for (const ReceivedRequest &request : server.received) {
    CAPTURE(request.raw);
    CHECK(request.post);
  }
}

TEST_CASE("A queued request counts as delivered only with a JSON answer that has a result") {
  Countly &countly = prepare();
  countly.setFailedRequestCooldown(1);
  start(countly);
  server.setAnswer([](const ReceivedRequest &) { return HTTPResponse{true, json(), 200, "<html>proxy login</html>"}; });
  countly.setUserDetails({{"name", "Alice"}});
  countly.processRQDebug();

  CHECK(server.queued().size() == 1);
  CHECK(countly.checkRQSize() == 1);
  json hc = countly.debugHealthCheck()->snapshot();
  CHECK(hc["sc"] == 200);
  CHECK(hc["em"] == "<html>proxy login</html>");

  SUBCASE("a JSON object without result is a failure too") {
    server.setAnswer([](const ReceivedRequest &) { return HTTPResponse{true, json({{"status", "ok"}}), 200, "{\"status\":\"ok\"}"}; });
    CHECK(eventually([&countly] { return server.queued().size() == 2; }, std::chrono::seconds(3), [&countly] { countly.processRQDebug(); }));
    CHECK(countly.checkRQSize() == 1);
  }

  SUBCASE("the request is sent again and removed once the server accepts it") {
    server.reset();
    CHECK(eventually([&countly] { return countly.checkRQSize() == 0; }, std::chrono::seconds(3), [&countly] { countly.processRQDebug(); }));
    REQUIRE(server.queued().size() == 1);
    CHECK(server.queued()[0].params["user_details"] == "{\"name\":\"Alice\"}");
  }
}

TEST_CASE("A failed request is tried again only after the cool-down") {
  SUBCASE("the default cool-down is 60 seconds") {
    Countly &countly = prepare();
    start(countly);
    server.setAnswer(failingAnswer);
    countly.setUserDetails({{"name", "Alice"}});
    countly.processRQDebug();
    countly.processRQDebug();
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
    CHECK(countly.checkRQSize() == 1);
  }

  SUBCASE("a shorter cool-down lets the request through after it") {
    Countly &countly = prepare();
    countly.setFailedRequestCooldown(1);
    start(countly);
    server.setAnswer(failingAnswer);
    countly.setUserDetails({{"name", "Alice"}});
    countly.processRQDebug();
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);

    server.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("a cool-down below 1 second is ignored") {
    Countly &countly = prepare();
    countly.setFailedRequestCooldown(0);
    CHECK(logCode(countly, "w121") == 1);
    CHECK(countly.getConfiguration().failedRequestCooldownSeconds == 60);
  }
}

TEST_CASE("Requests carry the number of requests waiting behind them") {
  Countly &countly = prepare();
  start(countly);
  countly.setUserDetails({{"name", "first"}});
  countly.setUserDetails({{"name", "second"}});
  countly.setUserDetails({{"name", "third"}});

  SUBCASE("queued requests") {
    countly.processRQDebug();
    std::vector<ReceivedRequest> queued = server.queued();
    REQUIRE(queued.size() == 3);
    CHECK(queued[0].params["rr"] == "2");
    CHECK(queued[1].params["rr"] == "1");
    CHECK(queued[2].params["rr"] == "0");
  }

  SUBCASE("an immediate request") {
    countly.updateRemoteConfig();
    REQUIRE(eventually(
        [] {
          std::lock_guard<std::mutex> lock(server.mtx);
          for (const ReceivedRequest &request : server.received) {
            if (request.path == "/o/sdk" && request.params.at("method") != "sc") {
              return true;
            }
          }
          return false;
        },
        std::chrono::seconds(5)));
    std::lock_guard<std::mutex> lock(server.mtx);
    bool remoteConfigSeen = false;
    for (const ReceivedRequest &request : server.received) {
      if (request.path == "/o/sdk" && request.params.at("method") != "sc") {
        CHECK(request.params.at("rr") == "3");
        remoteConfigSeen = true;
      }
    }
    CHECK(remoteConfigSeen);
  }

  SUBCASE("the checksum covers the rr parameter") {
    Countly::halt();
    Countly &salted = prepare();
    salted.setSalt("test-salt");
    start(salted);
    salted.setUserDetails({{"name", "salted"}});
    salted.processRQDebug();
    std::vector<ReceivedRequest> queued = server.queued();
    REQUIRE(queued.size() == 1);
    const std::string &raw = queued[0].raw;
    const std::string::size_type checksum = raw.rfind("&checksum256=");
    REQUIRE(checksum != std::string::npos);
    CHECK(raw.substr(0, checksum).find("&rr=0") != std::string::npos);
    CHECK(raw.substr(checksum + 13) == salted.calculateChecksum("test-salt", raw.substr(0, checksum)));
  }
}

TEST_CASE("A full request queue drops its oldest requests") {
  Countly &countly = prepare();
  countly.setMaxRequestQueueSize(3);
  start(countly);
  server.setAnswer(failingAnswer);
  for (int index = 0; index < 5; index++) {
    countly.setUserDetails({{"name", "user " + std::to_string(index)}});
  }
  CHECK(countly.checkRQSize() == 3);
  CHECK(logCode(countly, "w310") == 2);

  server.reset();
  countly.processRQDebug();
  std::vector<ReceivedRequest> queued = server.queued();
  REQUIRE(queued.size() == 3);
  CHECK(queued[0].params["user_details"] == "{\"name\":\"user 2\"}");
}

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("A queue far over its limit shrinks step by step") {
  // An earlier run left 250 requests; this run allows 10
  clearSDK();
  for (int index = 0; index < 250; index++) {
    storeRequest("app_key=" COUNTLY_TEST_APP_KEY "&device_id=old&timestamp=" + std::to_string(getUnixTimestamp()) + "&user_details=%7B%7D");
  }

  SUBCASE("by default at most 100 of the overflow plus one go per new request") {
    server.reset();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(scriptedClient);
    countly.disableHealthCheck();
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    countly.setMaxRequestQueueSize(10);
    start(countly);
    countly.setUserDetails({{"name", "new"}});
    CHECK(countly.checkRQSize() == 150);
    CHECK(logCode(countly, "w310") == 101);
  }

  SUBCASE("without the gradual cleaner the whole overflow goes at once") {
    server.reset();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(scriptedClient);
    countly.disableHealthCheck();
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    countly.setMaxRequestQueueSize(10);
    countly.disableGradualRequestCleaner();
    start(countly);
    countly.setUserDetails({{"name", "new"}});
    CHECK(countly.checkRQSize() == 10);
    CHECK(logCode(countly, "w310") == 241);
  }
}

TEST_CASE("Requests older than the drop age are removed without being sent") {
  const long long now = getUnixTimestamp();
  const std::string old_request = "app_key=" COUNTLY_TEST_APP_KEY "&device_id=old&timestamp=" + std::to_string(now - 3 * 3600 * 1000LL) + "&user_details=%7B%22name%22%3A%22old%22%7D";
  const std::string fresh_request = "app_key=" COUNTLY_TEST_APP_KEY "&device_id=old&timestamp=" + std::to_string(now - 1000) + "&user_details=%7B%22name%22%3A%22fresh%22%7D";
  const std::string untimed_request = "app_key=" COUNTLY_TEST_APP_KEY "&device_id=old&user_details=%7B%22name%22%3A%22untimed%22%7D";

  SUBCASE("set by the app") {
    clearSDK();
    storeRequest(old_request);
    storeRequest(fresh_request);
    storeRequest(untimed_request);
    server.reset();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(scriptedClient);
    countly.disableHealthCheck();
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    countly.setRequestDropAgeHours(2);
    start(countly);
    countly.processRQDebug();

    std::vector<ReceivedRequest> queued = server.queued();
    REQUIRE(queued.size() == 2);
    CHECK(queued[0].params["user_details"] == "{\"name\":\"fresh\"}");
    CHECK(queued[1].params["user_details"] == "{\"name\":\"untimed\"}");
    CHECK(logCode(countly, "w330") == 1);
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("set by the server") {
    clearSDK();
    storeRequest(old_request);
    storeRequest(fresh_request);
    server.reset();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(scriptedClient);
    countly.disableHealthCheck();
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    countly.enableImmediateRequestOnStop();
    countly.setUpdateInterval(600000);
    countly.setSDKBehaviorSettings("{\"dort\":2}");
    start(countly);
    countly.processRQDebug();

    std::vector<ReceivedRequest> queued = server.queued();
    REQUIRE(queued.size() == 1);
    CHECK(queued[0].params["user_details"] == "{\"name\":\"fresh\"}");
  }

  SUBCASE("without a drop age old requests are sent") {
    clearSDK();
    storeRequest(old_request);
    server.reset();
    Countly &countly = Countly::getInstance();
    countly.setHTTPClient(scriptedClient);
    countly.disableHealthCheck();
    countly.disableSDKBehaviorSettingsUpdates();
    countly.enableManualSessionControl();
    countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly.SetPath(TEST_DATABASE_NAME);
    start(countly);
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
  }
}

TEST_CASE("Turning tracking off stops recording but not sending") {
  clearSDK();
  storeRequest("app_key=" COUNTLY_TEST_APP_KEY "&device_id=old&timestamp=" + std::to_string(getUnixTimestamp()) + "&user_details=%7B%22name%22%3A%22stored%22%7D");
  server.reset();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(scriptedClient);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setSDKBehaviorSettings("{\"tracking\":false}");
  start(countly);
  countly.setUserDetails({{"name", "ignored"}});
  countly.processRQDebug();

  std::vector<ReceivedRequest> queued = server.queued();
  REQUIRE(queued.size() == 1);
  CHECK(queued[0].params["user_details"] == "{\"name\":\"stored\"}");
  CHECK(logCode(countly, "w505") == 1);
}
#endif

TEST_CASE("A slow answer pauses sending for the backoff duration") {
  SUBCASE("the server's settings set the backoff") {
    Countly &countly = prepare();
    countly.setSDKBehaviorSettings("{\"bom_at\":1,\"bom_d\":2}");
    start(countly);
    countly.setUserDetails({{"name", "first"}});
    countly.setUserDetails({{"name", "second"}});
    server.setAnswer([](const ReceivedRequest &) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      return HTTPResponse{true, json({{"result", "Success"}}), 200};
    });
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);

    json hc = countly.debugHealthCheck()->snapshot();
    CHECK(hc["bom"] == 1);
    CHECK(hc["cbom"] == 1);
    CHECK(logCode(countly, "w320") == 1);

    server.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
    CHECK(countly.checkRQSize() == 0);
  }

  SUBCASE("disableBackoffMechanism keeps sending") {
    Countly &countly = prepare();
    countly.disableBackoffMechanism();
    countly.setSDKBehaviorSettings("{\"bom_at\":1,\"bom_d\":2}");
    start(countly);
    countly.setUserDetails({{"name", "first"}});
    countly.setUserDetails({{"name", "second"}});
    server.setAnswer([](const ReceivedRequest &) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      return HTTPResponse{true, json({{"result", "Success"}}), 200};
    });
    countly.processRQDebug();
    CHECK(server.queued().size() == 2);
    CHECK(countly.debugHealthCheck()->snapshot()["bom"] == 0);
  }

  SUBCASE("the server's bom turns it on even when the app turned it off") {
    Countly &countly = prepare();
    countly.disableBackoffMechanism();
    countly.setSDKBehaviorSettings("{\"bom\":true,\"bom_at\":1,\"bom_d\":2}");
    start(countly);
    countly.setUserDetails({{"name", "first"}});
    countly.setUserDetails({{"name", "second"}});
    server.setAnswer([](const ReceivedRequest &) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      return HTTPResponse{true, json({{"result", "Success"}}), 200};
    });
    countly.processRQDebug();
    CHECK(server.queued().size() == 1);
  }

  SUBCASE("a fast answer keeps sending") {
    Countly &countly = prepare();
    countly.setSDKBehaviorSettings("{\"bom_at\":1,\"bom_d\":2}");
    start(countly);
    countly.setUserDetails({{"name", "first"}});
    countly.setUserDetails({{"name", "second"}});
    countly.processRQDebug();
    CHECK(server.queued().size() == 2);
    CHECK(countly.debugHealthCheck()->snapshot()["bom"] == 0);
  }
}

TEST_CASE("stop() sends the waiting requests also while a backoff pause runs") {
  clearSDK();
  server.reset();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(scriptedClient);
  countly.setSha256(fakeSha);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  // Without enableImmediateRequestOnStop, stop() closes by sending: the update loop sends the queue once more
  countly.setUpdateInterval(100);
  countly.setSDKBehaviorSettings("{\"bom_at\":1,\"bom_d\":60}");
  start(countly);

  server.setAnswer([](const ReceivedRequest &) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    return HTTPResponse{true, json({{"result", "Success"}}), 200};
  });
  countly.setUserDetails({{"name", "answered slowly"}});
  REQUIRE(eventually([&countly] { return logCode(countly, "w320") == 1; }, std::chrono::milliseconds(5000)));

  server.reset();
  const auto eventSent = [] {
    for (const ReceivedRequest &request : server.queued()) {
      if (request.params.count("events") && request.params.at("events").find("recorded_during_the_pause") != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  countly.RecordEvent("recorded_during_the_pause", 1);
  // A call that asks to send the stored requests waits for the pause to end
  countly.attemptToSendStoredRequests();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  REQUIRE_FALSE(eventSent());

  countly.stop();
  CHECK(eventSent());
  CHECK(countly.checkRQSize() == 0);
}
