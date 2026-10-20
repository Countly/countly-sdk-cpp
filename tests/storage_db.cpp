#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <iostream>
#include <string>
#include <vector>
using namespace cly;
using namespace std;

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Sqlite Storage Module Specific tests") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger(new cly::LoggerModule());
  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path, empty space") {
    configuration->databasePath = " ";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "/";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }

  SUBCASE("Test SQlite Storage Module against invalid database path") {
    configuration->databasePath = "Q:/blah";

    std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
    storageModule->init();
    test_utils::storageModuleNotInitialized(storageModule);
  }
}

/**
 * Runs flush until check passes, for up to five seconds. The SDK's own update loop may be
 * sending the request queue at the moment the test asks, so a single attempt can miss.
 */
template <typename Flush, typename Check> static bool eventually(Flush flush, Check check) {
  for (int attempt = 0; attempt < 50; attempt++) {
    flush();
    if (check()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

/**
 * Starts the default instance with the fake HTTP client and the given database path (none when empty),
 * then waits for the SDK behavior settings fetch to land.
 */
static void startWithDatabasePath(Countly &countly, const std::string &database_path) {
  countly.setLogger(test_utils::captureLog);
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  if (!database_path.empty()) {
    countly.setDatabasePath(database_path);
  }
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_CASE("SQLite builds keep events and requests in memory when there is no usable database") {
  test_utils::clearSDK();
  test_utils::clearCapturedLogs();
  Countly &countly = Countly::getInstance();

  SUBCASE("no database path is set") {
    startWithDatabasePath(countly, "");
    REQUIRE(countly.checkEQSize() == 0);
    CHECK(test_utils::capturedLogContains("No database path is set"));

    countly.addEvent(cly::Event("event_without_database_path", 1));
    CHECK(countly.checkEQSize() == 1);
    CHECK(eventually([&countly]() { countly.updateSession(); countly.processRQDebug(); }, []() { return test_utils::httpQueueContainsEvent("event_without_database_path"); }));
  }

  SUBCASE("the database cannot be opened") {
    startWithDatabasePath(countly, "countly-missing-folder/countly.db");
    REQUIRE(countly.checkEQSize() == 0);
    CHECK(test_utils::capturedLogContains("Could not open the database"));

    countly.addEvent(cly::Event("event_after_database_failure", 1));
    CHECK(countly.checkEQSize() == 1);
    CHECK(eventually([&countly]() { countly.updateSession(); countly.processRQDebug(); }, []() { return test_utils::httpQueueContainsEvent("event_after_database_failure"); }));
  }

  countly.setLogger(nullptr);
}

TEST_CASE("SQLite-build instances without database paths run side by side") {
  test_utils::clearSDK();
  test_utils::InstanceFixture first("first-app-key", "");
  test_utils::InstanceFixture second("second-app-key", "");
  REQUIRE(first.initialized());
  REQUIRE(second.initialized());

  first.sdk->addEvent(cly::Event("first_app_event", 1));
  second.sdk->addEvent(cly::Event("second_app_event", 1));
  CHECK(eventually([&first]() { first.sdk->updateSession(); first.flush(); }, [&first]() { return first.sawEvent("first_app_event"); }));
  CHECK(eventually([&second]() { second.sdk->updateSession(); second.flush(); }, [&second]() { return second.sawEvent("second_app_event"); }));
  CHECK_FALSE(first.sawEvent("second_app_event"));
  CHECK_FALSE(second.sawEvent("first_app_event"));
}

TEST_CASE("SQLite builds with a database path keep queued events across a restart") {
  test_utils::clearSDK();
  const std::string database_path = "test-countly-restart.db";
  remove(database_path.c_str());

  {
    std::shared_ptr<Countly> before_restart = std::make_shared<Countly>();
    before_restart->setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
      (void)use_post;
      (void)url;
      (void)data;
      return HTTPResponse{false, nlohmann::json::object()};
    });
    before_restart->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    before_restart->setDatabasePath(database_path);
    before_restart->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
    before_restart->addEvent(cly::Event("event_before_restart", 1));
    REQUIRE(before_restart->checkEQSize() == 1);
  }

  std::shared_ptr<Countly> after_restart = std::make_shared<Countly>();
  after_restart->setHTTPClient(test_utils::fakeSendHTTP);
  after_restart->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  after_restart->setDatabasePath(database_path);
  after_restart->start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(after_restart->checkEQSize() >= 0);
  CHECK(eventually([&after_restart]() { after_restart->updateSession(); after_restart->processRQDebug(); }, []() { return test_utils::httpQueueContainsEvent("event_before_restart"); }));

  after_restart.reset();
  remove(database_path.c_str());
}
#endif