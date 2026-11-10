#include <regex>
#include <stdexcept>
#include <string>

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
 * Whether the text is a version 4 UUID in lower case.
 */
bool isUuidV4(const std::string &text) {
  static const std::regex pattern("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  return std::regex_match(text, pattern);
}

/**
 * The count of a log code in the health check counters, 0 when it was not recorded.
 */
long long logCodeCount(Countly &countly, const std::string &code) {
  json hc = countly.debugHealthCheck()->snapshot();
  return hc["lc"].contains(code) ? hc["lc"][code].get<long long>() : 0;
}

/**
 * Prepares the default instance with the fake HTTP client and no health check request.
 */
Countly &prepareInstance() {
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.SetPath(TEST_DATABASE_NAME);
  return countly;
}

/**
 * The device_id and t parameters of the first begin_session request the fake client received.
 */
std::pair<std::string, std::string> beginSessionIdentity() {
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("begin_session")) {
      return {call.data["device_id"], call.data.count("t") ? call.data["t"] : std::string("<none>")};
    }
  }
  return {"<no begin_session>", "<none>"};
}

void throwingLogger(LogLevel, const std::string &) { throw std::runtime_error("listener failed"); }

#ifdef COUNTLY_USE_SQLITE
const char *const GIVEN_ID_IGNORED = "the device ID given with setDeviceID is ignored";

/**
 * Whether any request the fake client received was sent under the device ID.
 */
bool anyRequestFor(const std::string &device_id) {
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    if (http_call_queue.at(index).data["device_id"] == device_id) {
      return true;
    }
  }
  return false;
}

/**
 * Writes an event into the events table of the test database, as an app that ended without stop() leaves it.
 */
void storeEventInDatabase(const std::string &event) {
  sqlite3 *database = nullptr;
  REQUIRE(sqlite3_open(TEST_DATABASE_NAME, &database) == SQLITE_OK);
  sqlite3_stmt *statement = nullptr;
  REQUIRE(sqlite3_prepare_v2(database, "INSERT INTO events (event) VALUES(?);", -1, &statement, nullptr) == SQLITE_OK);
  sqlite3_bind_text(statement, 1, event.c_str(), -1, SQLITE_TRANSIENT);
  CHECK(sqlite3_step(statement) == SQLITE_DONE);
  sqlite3_finalize(statement);
  sqlite3_close(database);
}
#endif
} // namespace

TEST_CASE("Device ID when the app gives none") {
  SUBCASE("start() generates a UUID, sends it with type 1 and getDeviceID returns it") {
    clearSDK();
    Countly &countly = prepareInstance();
    CHECK_NOTHROW(countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false));
    REQUIRE(countly.checkEQSize() == 0);
    countly.processRQDebug();

    std::pair<std::string, std::string> identity = beginSessionIdentity();
    CHECK(isUuidV4(identity.first));
    CHECK(identity.second == "1");
    CHECK(countly.getDeviceID() == identity.first);
    CHECK(countly.getDeviceIDType() == DeviceIdType::SDK_GENERATED);
  }

  SUBCASE("an ID given with setDeviceID is sent with type 0") {
    clearSDK();
    Countly &countly = prepareInstance();
    countly.setDeviceID("developer-id");
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly.processRQDebug();

    std::pair<std::string, std::string> identity = beginSessionIdentity();
    CHECK(identity.first == "developer-id");
    CHECK(identity.second == "0");
    CHECK(countly.getDeviceIDType() == DeviceIdType::DEVELOPER_SUPPLIED);
  }

#ifdef COUNTLY_USE_SQLITE
  SUBCASE("a generated ID is kept for the next starts, also when an ID is given later") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    const std::string generated = countly->getDeviceID();
    REQUIRE(isUuidV4(generated));

    Countly::halt();
    http_call_queue.clear();
    countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->getDeviceID() == generated);
    CHECK(countly->getDeviceIDType() == DeviceIdType::SDK_GENERATED);

    Countly::halt();
    clearCapturedLogs();
    countly = &prepareInstance();
    countly->setLogger(captureLog);
    countly->setDeviceID("developer-id");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly->processRQDebug();
    CHECK(countly->getDeviceID() == generated);
    CHECK(countly->getDeviceIDType() == DeviceIdType::SDK_GENERATED);
    CHECK_FALSE(anyRequestFor("developer-id"));
    CHECK(capturedLogContains(GIVEN_ID_IGNORED));
  }

  SUBCASE("an ID given at the first start is kept: given again it is used quietly, a different one is ignored") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->setDeviceID("first-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

    Countly::halt();
    clearCapturedLogs();
    countly = &prepareInstance();
    countly->setLogger(captureLog);
    countly->setDeviceID("first-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->getDeviceID() == "first-user");
    CHECK_FALSE(capturedLogContains(GIVEN_ID_IGNORED));

    Countly::halt();
    clearCapturedLogs();
    http_call_queue.clear();
    countly = &prepareInstance();
    countly->setLogger(captureLog);
    countly->setDeviceID("second-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly->processRQDebug();
    CHECK(countly->getDeviceID() == "first-user");
    CHECK(countly->getDeviceIDType() == DeviceIdType::DEVELOPER_SUPPLIED);
    CHECK_FALSE(anyRequestFor("second-user"));
    CHECK(capturedLogContains(GIVEN_ID_IGNORED));
  }

  SUBCASE("enableClearStoredDeviceID makes start() use the given ID, which the next start keeps") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->setDeviceID("first-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

    Countly::halt();
    countly = &prepareInstance();
    countly->enableClearStoredDeviceID();
    countly->setDeviceID("second-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->getDeviceID() == "second-user");
    CHECK(countly->getDeviceIDType() == DeviceIdType::DEVELOPER_SUPPLIED);

    Countly::halt();
    countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->getDeviceID() == "second-user");
  }

  SUBCASE("enableClearStoredDeviceID without a given ID generates a new ID") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    const std::string first = countly->getDeviceID();
    REQUIRE(isUuidV4(first));

    Countly::halt();
    countly = &prepareInstance();
    countly->enableClearStoredDeviceID();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(isUuidV4(countly->getDeviceID()));
    CHECK(countly->getDeviceID() != first);
    CHECK(countly->getDeviceIDType() == DeviceIdType::SDK_GENERATED);
  }

  SUBCASE("enableClearStoredDeviceID first sends the events the stored user left behind under the stored ID") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->setDeviceID("first-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    Countly::halt();
    storeEventInDatabase("{\"key\":\"left_behind\",\"count\":1,\"timestamp\":1700000000000,\"hour\":10,\"dow\":2}");
    http_call_queue.clear();

    countly = &prepareInstance();
    countly->enableClearStoredDeviceID();
    countly->setDeviceID("second-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->checkEQSize() == 0);
    countly->processRQDebug();

    std::vector<HTTPCall> left_behind;
    for (size_t index = 0; index < http_call_queue.size(); index++) {
      HTTPCall call = http_call_queue.at(index);
      if (call.data.count("events") && call.data["events"].find("left_behind") != std::string::npos) {
        left_behind.push_back(call);
      }
    }
    REQUIRE(left_behind.size() == 1);
    CHECK(left_behind[0].data["device_id"] == "first-user");
    CHECK(left_behind[0].data["t"] == "0");
    CHECK(countly->getDeviceID() == "second-user");
  }

  SUBCASE("an ID changed after start is kept for the next start") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->setDeviceID("first-user");
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly->setDeviceID("second-user", false);

    Countly::halt();
    countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly->getDeviceID() == "second-user");
  }
#else
  SUBCASE("without SQLite a new ID is generated at every start") {
    clearSDK();
    Countly *countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    const std::string first = countly->getDeviceID();

    Countly::halt();
    countly = &prepareInstance();
    countly->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(isUuidV4(countly->getDeviceID()));
    CHECK(countly->getDeviceID() != first);
  }
#endif

  SUBCASE("enableClearStoredDeviceID after start() is ignored with a warning") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = prepareInstance();
    countly.setLogger(captureLog);
    countly.setDeviceID("first-user");
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly.enableClearStoredDeviceID();
    CHECK(capturedLogContains("enableClearStoredDeviceID, This method can't be called after SDK initialization"));
    CHECK(countly.getDeviceID() == "first-user");
  }
}

TEST_CASE("An empty device ID is ignored") {
  SUBCASE("before start, the SDK generates an ID instead") {
    clearSDK();
    Countly &countly = prepareInstance();
    countly.setDeviceID("");
    CHECK(logCodeCount(countly, "w601") == 1);
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(isUuidV4(countly.getDeviceID()));
  }

  SUBCASE("after start, the device ID stays") {
    clearSDK();
    Countly &countly = prepareInstance();
    countly.setDeviceID("developer-id");
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    countly.processRQDebug();
    http_call_queue.clear();

    countly.setDeviceID("", false);
    countly.setDeviceID("", true);
    countly.RecordEvent("after_empty_id", 1);
    countly.flushEvents();
    countly.processRQDebug();

    CHECK(countly.getDeviceID() == "developer-id");
    CHECK(logCodeCount(countly, "w601") == 2);
    CHECK_FALSE(httpQueueContains("device_id", ""));
    CHECK_FALSE(httpQueueContains("end_session", "1"));
    CHECK(httpQueueContainsEvent("after_empty_id"));
  }
}

TEST_CASE("start() refuses a missing or placeholder app key") {
  const std::vector<std::string> app_keys = {"", "YOUR_APP_KEY"};
  for (const std::string &app_key : app_keys) {
    CAPTURE(app_key);
    clearSDK();
    clearCapturedLogs();
    Countly &countly = prepareInstance();
    countly.setLogger(captureLog);
    countly.setDeviceID("developer-id");

    CHECK_NOTHROW(countly.start(app_key, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false));
    CHECK(countly.checkEQSize() == -1);
    CHECK(capturedLogContains("app key"));
    countly.RecordEvent("ignored", 1);
    CHECK(http_call_queue.empty());

    // the app can fix the key and start again
    countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    CHECK(countly.checkEQSize() == 0);
    countly.setLogger(nullptr);
  }
}

TEST_CASE("start() refuses a server URL that is not an absolute http or https URL") {
  const std::vector<std::string> urls = {"", "try.count.ly", "ftp://try.count.ly", "https://", "http:///i"};
  for (const std::string &url : urls) {
    CAPTURE(url);
    clearSDK();
    clearCapturedLogs();
    Countly &countly = prepareInstance();
    countly.setLogger(captureLog);
    countly.setDeviceID("developer-id");

    CHECK_NOTHROW(countly.start(COUNTLY_TEST_APP_KEY, url, COUNTLY_TEST_PORT, false));
    CHECK(countly.checkEQSize() == -1);
    CHECK(capturedLogContains("server URL"));
    countly.setLogger(nullptr);
  }

  SUBCASE("a valid URL loses its trailing slashes with a warning") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = prepareInstance();
    countly.setLogger(captureLog);
    countly.setDeviceID("developer-id");
    countly.start(COUNTLY_TEST_APP_KEY, "HTTPS://try.count.ly//", COUNTLY_TEST_PORT, false);

    CHECK(countly.checkEQSize() == 0);
    CHECK(countly.getConfiguration().serverUrl == "https://try.count.ly");
    CHECK(capturedLogContains("trailing"));
    countly.setLogger(nullptr);
  }
}

TEST_CASE("views() and crash() can be used before start()") {
  clearSDK();
  Countly &countly = prepareInstance();
  countly.setDeviceID("developer-id");

  CHECK(countly.views().openView("BeforeStart").empty());
  countly.views().closeViewWithName("BeforeStart");
  countly.crash().addBreadcrumb("before start");
  countly.crash().recordException("Before start", "frame", true, {{"_os", "Linux"}, {"_app_version", "1.0"}});
  CHECK(logCodeCount(countly, "w101") == 4);

  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.crash().recordException("After start", "frame", false, {{"_os", "Linux"}, {"_app_version", "1.0"}});
  countly.processRQDebug();

  bool crashSeen = false;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("crash")) {
      json crash = json::parse(call.data["crash"]);
      CHECK(crash["_name"] == "After start");
      CHECK(crash["_logs"] == "");
      crashSeen = true;
    }
  }
  CHECK(crashSeen);
  CHECK_FALSE(httpQueueContainsEvent("[CLY]_view"));
}

TEST_CASE("A log listener that throws does not break the SDK") {
  clearSDK();
  Countly &countly = prepareInstance();
  countly.setLogger(throwingLogger);
  countly.setDeviceID("developer-id");

  CHECK_NOTHROW(countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false));
  CHECK_NOTHROW(countly.RecordEvent("still_recorded", 1));
  CHECK_NOTHROW(countly.flushEvents());
  CHECK_NOTHROW(countly.processRQDebug());
  CHECK(httpQueueContainsEvent("still_recorded"));
  countly.setLogger(nullptr);
}

TEST_CASE("Config values below their minimum are ignored") {
  clearSDK();
  Countly &countly = prepareInstance();
  countly.setMaxKeyLength(0);
  countly.setMaxValueSize(0);
  countly.setMaxSegmentationValues(0);
  countly.setMaxBreadcrumbCount(0);
  countly.setMaxStackTraceLinesPerThread(0);
  countly.setMaxStackTraceLineLength(0);
  countly.setMaxRequestQueueSize(0);
  countly.setAutomaticSessionUpdateInterval(0);
  countly.setEventsToRQThreshold(0);
  CHECK(logCodeCount(countly, "w121") == 9);

  const CountlyConfiguration &config = countly.getConfiguration();
  CHECK(config.maxKeyLength == COUNTLY_MAX_KEY_LENGTH_DEFAULT);
  CHECK(config.maxValueSize == COUNTLY_MAX_VALUE_SIZE_DEFAULT);
  CHECK(config.maxSegmentationValues == COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT);
  CHECK(config.breadcrumbsThreshold == COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT);
  CHECK(config.maxStackTraceLinesPerThread == COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT);
  CHECK(config.maxStackTraceLineLength == COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT);
  CHECK(config.requestQueueThreshold == 1000);
  CHECK(config.sessionDuration == 60);
  CHECK(config.eventQueueThreshold == 100);

  countly.setDeviceID("developer-id");
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.RecordEvent("purchase", {{"color", "red"}}, 1);
  countly.flushEvents();
  countly.processRQDebug();
  CHECK(httpQueueContainsEvent("purchase"));

  SUBCASE("a large event queue threshold is kept as given") {
    countly.setEventsToRQThreshold(20000);
    CHECK(countly.getConfiguration().eventQueueThreshold == 20000);
  }
}

TEST_CASE("User details given before start() are kept for the first session without a warning") {
  clearSDK();
  Countly &countly = prepareInstance();
  countly.setUserDetails({{"name", "Alice"}});
  countly.setCustomUserDetails({{"plan", "gold"}});
  CHECK(logCodeCount(countly, "w101") == 0);

  countly.setDeviceID("developer-id");
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.processRQDebug();
  bool sent = false;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("begin_session")) {
      CHECK(json::parse(call.data["user_details"]) == json({{"name", "Alice"}, {"custom", {{"plan", "gold"}}}}));
      sent = true;
    }
  }
  CHECK(sent);
}

TEST_CASE("Cut user detail values are logged as warnings") {
  clearSDK();
  Countly &countly = prepareInstance();
  countly.setSDKBehaviorSettings("{\"lvs\":5,\"lkl\":4}");
  countly.setDeviceID("developer-id");
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  countly.processRQDebug();
  http_call_queue.clear();

  countly.setUserDetails({{"name", "Alexander"}});
  countly.setCustomUserDetails({{"favourite", "chocolate"}});
  countly.processRQDebug();
  CHECK(httpQueueContains("user_details", "{\"name\":\"Alexa\"}"));
  CHECK(httpQueueContains("user_details", "{\"custom\":{\"favo\":\"choco\"}}"));
  CHECK(logCodeCount(countly, "w202") == 2);
  CHECK(logCodeCount(countly, "w201") == 1);
}
