#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"

#include "countly/constants.hpp"
#include "countly/path_utils.hpp"
#include "countly/request_module.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;

// Countly::setLogger takes a raw function pointer, not a std::function, so a
// capturing lambda will not convert.
static void multiInstanceTestLogger(cly::LogLevel level, const std::string &message) {
  (void)level;
  (void)message;
}

TEST_CASE("normalizeDatabasePath collapses equivalent spellings") {
  // All inputs are lowercase so the expectations hold on both case-sensitive
  // and case-insensitive platforms. Case folding is covered separately below.
  CHECK(utils::normalizeDatabasePath("a.db") == "a.db");
  CHECK(utils::normalizeDatabasePath("./a.db") == "a.db");
  CHECK(utils::normalizeDatabasePath(".\\a.db") == "a.db");
  CHECK(utils::normalizeDatabasePath("data//a.db") == "data/a.db");
  CHECK(utils::normalizeDatabasePath("data/./a.db") == "data/a.db");
  CHECK(utils::normalizeDatabasePath("data/../data/a.db") == "data/a.db");
  CHECK(utils::normalizeDatabasePath("data\\sub\\..\\a.db") == "data/a.db");
  CHECK(utils::normalizeDatabasePath("  a.db  ") == "a.db");
  CHECK(utils::normalizeDatabasePath("data/") == "data");
  CHECK(utils::normalizeDatabasePath("/var/db/a.db") == "/var/db/a.db");
  CHECK(utils::normalizeDatabasePath("//server/share/a.db") == "//server/share/a.db");
  CHECK(utils::normalizeDatabasePath("../a.db") == "../a.db");
  CHECK(utils::normalizeDatabasePath("c:/data/a.db") == "c:/data/a.db");
  CHECK(utils::normalizeDatabasePath("") == "");
  CHECK(utils::normalizeDatabasePath("   ") == "");
}

#ifdef _WIN32
TEST_CASE("normalizeDatabasePath folds case on Windows") {
  CHECK(utils::normalizeDatabasePath("A.DB") == "a.db");
  CHECK(utils::normalizeDatabasePath("C:\\Data\\A.db") == "c:/data/a.db");
}
#endif

TEST_CASE("generateEventID varies its random component") {
  // The old implementation bound a *copy* of a const engine on every call, so
  // the random half was constant for the whole process and only the timestamp
  // moved. Assert on the random half directly: a uniqueness check on the whole
  // ID can pass by accident on platforms with a fine-grained clock.
  std::set<std::string> random_parts;
  std::set<std::string> ids;
  for (int i = 0; i < 1000; i++) {
    const std::string id = utils::generateEventID();
    ids.insert(id);
    random_parts.insert(id.substr(0, id.find('_')));
  }
  CHECK(random_parts.size() > 1);
  CHECK(ids.size() == 1000);
}

TEST_CASE("localTime and gmTime are safe to call from several threads") {
  // Event::setTimestamp and RequestBuilder::buildRequest used std::localtime and
  // std::gmtime, which return a pointer into one process-wide std::tm. Two
  // threads racing there could copy a struct the other had already overwritten,
  // and because localtime and gmtime share the buffer a "local" tm could come
  // back holding GMT fields -- a wrong tz/dow/hour on the wire. Every instance
  // runs its own update loop, so more than one instance is enough to hit it.
  const std::time_t fixed_time = 1700000000;
  const std::tm expected_local = utils::localTime(fixed_time);
  const std::tm expected_gm = utils::gmTime(fixed_time);

  std::atomic<int> mismatches(0);
  std::vector<std::thread> workers;
  for (int t = 0; t < 8; t++) {
    const bool use_local = (t % 2) == 0;
    workers.emplace_back([use_local, fixed_time, &expected_local, &expected_gm, &mismatches]() {
      for (int i = 0; i < 2000; i++) {
        const std::tm actual = use_local ? utils::localTime(fixed_time) : utils::gmTime(fixed_time);
        const std::tm &expected = use_local ? expected_local : expected_gm;
        if (actual.tm_hour != expected.tm_hour || actual.tm_min != expected.tm_min || actual.tm_wday != expected.tm_wday || actual.tm_mday != expected.tm_mday) {
          mismatches.fetch_add(1);
        }
      }
    });
  }
  for (std::thread &worker : workers) {
    worker.join();
  }

  CHECK(mismatches.load() == 0);
}

TEST_CASE("two instances produce distinct view IDs for the same view name") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-viewid-a.db");
  InstanceFixture b("APP_KEY_B", "mi-viewid-b.db");
  REQUIRE(a.initialized());
  REQUIRE(b.initialized());

  const std::string idA = a.sdk->views().openView("home");
  const std::string idB = b.sdk->views().openView("home");

  CHECK(idA != "");
  CHECK(idB != "");
  CHECK(idA != idB);
}

TEST_CASE("events recorded on one instance never reach the other") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-events-a.db");
  InstanceFixture b("APP_KEY_B", "mi-events-b.db");
  REQUIRE(a.initialized());
  REQUIRE(b.initialized());
  a.clearCalls();
  b.clearCalls();

  a.sdk->addEvent(cly::Event("click", 1));
  b.sdk->addEvent(cly::Event("purchase", 1));
  a.sdk->flushEvents();
  b.sdk->flushEvents();
  a.flush();
  b.flush();

  CHECK(a.sawEvent("click"));
  CHECK_FALSE(a.sawEvent("purchase"));
  CHECK(b.sawEvent("purchase"));
  CHECK_FALSE(b.sawEvent("click"));
  CHECK(a.sawKeyValue("app_key", "APP_KEY_A"));
  CHECK_FALSE(a.sawKeyValue("app_key", "APP_KEY_B"));
  CHECK(b.sawKeyValue("app_key", "APP_KEY_B"));
  CHECK_FALSE(b.sawKeyValue("app_key", "APP_KEY_A"));
}

TEST_CASE("each instance's session carries its own app key and device id") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-session-a.db", "device-a");
  InstanceFixture b("APP_KEY_B", "mi-session-b.db", "device-b");
  REQUIRE(a.initialized());
  REQUIRE(b.initialized());
  a.flush();
  b.flush();

  CHECK(a.sawKeyValue("device_id", "device-a"));
  CHECK_FALSE(a.sawKeyValue("device_id", "device-b"));
  CHECK(b.sawKeyValue("device_id", "device-b"));
  CHECK_FALSE(b.sawKeyValue("device_id", "device-a"));
}

TEST_CASE("destroying one instance leaves the other working") {
  clearSDK();
  InstanceFixture b("APP_KEY_B", "mi-survive-b.db");
  REQUIRE(b.initialized());
  {
    InstanceFixture a("APP_KEY_A", "mi-survive-a.db");
    REQUIRE(a.initialized());
    a.sdk->addEvent(cly::Event("click", 1));
  }
  // a is gone; b must still record and deliver.
  b.clearCalls();
  b.sdk->addEvent(cly::Event("purchase", 1));
  b.sdk->flushEvents();
  b.flush();

  CHECK(b.initialized());
  CHECK(b.sawEvent("purchase"));
}

TEST_CASE("curl global state is initialised once and survives instance destruction") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-curl-a.db");
  REQUIRE(a.initialized());

  {
    InstanceFixture b("APP_KEY_B", "mi-curl-b.db");
    REQUIRE(b.initialized());

    // The point of CurlGlobal is the gap between these two numbers: several
    // requests to initialise, exactly one initialisation. Asserting the actual
    // count alone would be vacuous -- it is 1 by construction.
    CHECK(cly::RequestModule::globalNetworkingInitRequests() >= 2);
    CHECK(cly::RequestModule::globalNetworkingInitCount() == 1);
  }

  // b is destroyed. Networking must not have been torn down under a.
  CHECK(cly::RequestModule::globalNetworkingReleased() == false);
  CHECK(a.initialized());
  CHECK(cly::RequestModule::globalNetworkingInitCount() == 1);
}

TEST_CASE("a second remote config fetch is dropped rather than blocking the caller") {
  clearSDK();
  static std::atomic<int> fetches_started(0);
  static std::atomic<bool> release_fetch(false);
  fetches_started.store(0);
  release_fetch.store(false);

  std::shared_ptr<cly::Countly> sdk = std::make_shared<cly::Countly>();
  sdk->setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
    (void)use_post;
    (void)url;
    cly::HTTPResponse response;
    response.success = true;
    response.data = nlohmann::json::object();
    if (data.find("fetch_remote_config") != std::string::npos) {
      fetches_started.fetch_add(1);
      while (!release_fetch.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
    return response;
  });
  sdk->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  sdk->SetPath("mi-rc-drop.db");
  sdk->disableSDKBehaviorSettingsUpdates();
  sdk->enableImmediateRequestOnStop();
  sdk->enableRemoteConfig();
  sdk->start("APP_KEY_RC2", COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(sdk->checkEQSize() == 0);

  sdk->updateRemoteConfig();
  // Wait for the first fetch to be genuinely in flight and parked.
  for (int i = 0; i < 200 && fetches_started.load() == 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(fetches_started.load() == 1);

  // The second call must return immediately instead of waiting for the first.
  const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
  sdk->updateRemoteConfig();
  const long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();

  CHECK(elapsed_ms < 200);            // did not block on the parked fetch
  CHECK(fetches_started.load() == 1); // and did not start a second one

  release_fetch.store(true);
  sdk.reset();
  remove("mi-rc-drop.db");
}

TEST_CASE("named instances are distinct objects and are found by name") {
  clearSDK();
  cly::Countly &a = cly::Countly::createInstance("appA");
  cly::Countly &b = cly::Countly::createInstance("appB");

  CHECK(&a != &b);
  CHECK(&cly::Countly::getInstance("appA") == &a);
  CHECK(&cly::Countly::getInstance("appB") == &b);
  CHECK(&cly::Countly::getInstance() != &a);
  CHECK(&cly::Countly::getInstance() != &b);

  CHECK(cly::Countly::hasInstance("appA"));
  CHECK(cly::Countly::findInstance("appA") == &a);
}

TEST_CASE("the empty name is the default instance") {
  clearSDK();
  CHECK(&cly::Countly::getInstance("") == &cly::Countly::getInstance());
}

TEST_CASE("an unknown name is not reported as present") {
  clearSDK();
  CHECK_FALSE(cly::Countly::hasInstance("nope"));
  CHECK(cly::Countly::findInstance("nope") == nullptr);
}

TEST_CASE("destroyInstance removes the instance") {
  clearSDK();
  cly::Countly::createInstance("appA");
  REQUIRE(cly::Countly::hasInstance("appA"));

  cly::Countly::destroyInstance("appA");

  CHECK_FALSE(cly::Countly::hasInstance("appA"));
  CHECK(cly::Countly::findInstance("appA") == nullptr);
  cly::Countly::destroyInstance("appA"); // destroying an absent name is a no-op
  CHECK_FALSE(cly::Countly::hasInstance("appA"));
}

TEST_CASE("destroyAllInstances clears the registry") {
  clearSDK();
  cly::Countly::createInstance("appA");
  cly::Countly::createInstance("appB");

  cly::Countly::destroyAllInstances();

  CHECK_FALSE(cly::Countly::hasInstance("appA"));
  CHECK_FALSE(cly::Countly::hasInstance("appB"));
}

TEST_CASE("a registry-created instance inherits the default instance's logger") {
  clearSDK();
  cly::Countly::getInstance().setLogger(multiInstanceTestLogger);
  REQUIRE(cly::Countly::getInstance().getLogger() != nullptr);

  cly::Countly &named = cly::Countly::createInstance("appLogger");

  CHECK(named.getLogger() != nullptr);
}

TEST_CASE("shutdownNetworking refuses while an instance is live") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-shutdown-a.db");
  REQUIRE(a.initialized());
  REQUIRE(cly::Countly::debugLiveInstanceCount() > 0);

  cly::Countly::shutdownNetworking();

  CHECK(cly::RequestModule::globalNetworkingReleased() == false);
  CHECK(a.initialized());
}

TEST_CASE("a remote config fetch thread is joined by destruction") {
  clearSDK();
  static std::atomic<bool> fetch_completed(false);
  fetch_completed.store(false);

  std::shared_ptr<cly::Countly> sdk = std::make_shared<cly::Countly>();
  // A slow client: if the fetch thread is detached, destruction returns before
  // the store below runs, which is exactly the bug this asserts against.
  sdk->setHTTPClient([](bool use_post, const std::string &url, const std::string &data) {
    (void)use_post;
    (void)url;
    cly::HTTPResponse response;
    response.success = true;
    response.data = nlohmann::json::object();
    // Only the remote-config fetch is slowed down. The SDK Behavior Settings
    // fetch also targets /o/sdk (with method=sc), and delaying that one too
    // would let the SBS thread set the flag and make this test meaningless.
    if (data.find("fetch_remote_config") != std::string::npos) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      fetch_completed.store(true);
    }
    return response;
  });
  sdk->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  sdk->SetPath("mi-remote-config.db");
  sdk->disableSDKBehaviorSettingsUpdates(); // no periodic SBS thread in this test
  // Without this, ~Countly blocks for up to COUNTLY_KEEPALIVE_INTERVAL (3s)
  // joining the update loop, and that incidental wait is long enough for a
  // *detached* fetch thread to finish -- which would make this test pass
  // whether the thread is owned or not. With it, destruction returns promptly,
  // so only an owned-and-joined thread can have set the flag.
  sdk->enableImmediateRequestOnStop();
  sdk->enableRemoteConfig();
  sdk->start("APP_KEY_RC", COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(sdk->checkEQSize() == 0);

  fetch_completed.store(false);
  sdk->updateRemoteConfig();
  sdk.reset(); // must join the fetch thread

  CHECK(fetch_completed.load() == true);
  remove("mi-remote-config.db");
}

#ifdef COUNTLY_USE_SQLITE

// Builds a second instance by hand, because InstanceFixture assumes start()
// succeeds and here we need to observe it being refused.
static std::shared_ptr<cly::Countly> startInstanceAt(const std::string &app_key, const std::string &db_path) {
  std::shared_ptr<cly::Countly> sdk = std::make_shared<cly::Countly>();
  sdk->setHTTPClient(test_utils::fakeSendHTTP);
  sdk->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  sdk->SetPath(db_path);
  sdk->start(app_key, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  return sdk;
}

TEST_CASE("a second instance on a claimed database path is refused") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-claim.db");
  REQUIRE(a.initialized());

  std::shared_ptr<cly::Countly> b = startInstanceAt("APP_KEY_B", "mi-claim.db");

  CHECK(b->checkEQSize() == -1); // refused: never initialized
  CHECK(a.initialized());        // the first instance is unaffected
  b.reset();
}

TEST_CASE("an equivalent spelling of a claimed path is refused") {
  clearSDK();
  InstanceFixture a("APP_KEY_A", "mi-equiv.db");
  REQUIRE(a.initialized());

  std::shared_ptr<cly::Countly> b = startInstanceAt("APP_KEY_B", "./mi-equiv.db");

  CHECK(b->checkEQSize() == -1);
  CHECK(a.initialized());
  b.reset();
}

TEST_CASE("a database path is released when its instance is destroyed") {
  clearSDK();
  {
    InstanceFixture a("APP_KEY_A", "mi-release.db");
    REQUIRE(a.initialized());
  }
  InstanceFixture b("APP_KEY_B", "mi-release.db");
  CHECK(b.initialized());
}

TEST_CASE("halt releases the default instance's path claim") {
  clearSDK();
  cly::Countly &d = cly::Countly::getInstance();
  d.setHTTPClient(test_utils::fakeSendHTTP);
  d.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  d.SetPath(TEST_DATABASE_NAME);
  d.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(d.checkEQSize() == 0);

  clearSDK(); // halt() -> destroyAllInstances()

  CHECK(cly::Countly::debugClaimedPathCount() == 0);
}

TEST_CASE("a failed initialisation leaves no stale path claim") {
  clearSDK();
  const size_t before = cly::Countly::debugClaimedPathCount();

  // Blank path: refused before any claim is made.
  std::shared_ptr<cly::Countly> blank = startInstanceAt("APP_KEY_A", "");
  CHECK(blank->checkEQSize() == -1);
  CHECK(cly::Countly::debugClaimedPathCount() == before);
  blank.reset();

  // Unwritable path: the claim is taken, then the schema creation fails, so the
  // claim must be given back.
  std::shared_ptr<cly::Countly> bad = startInstanceAt("APP_KEY_A", "mi-no-such-dir/x.db");
  CHECK(bad->checkEQSize() == -1);
  CHECK(cly::Countly::debugClaimedPathCount() == before);
  bad.reset();
}

TEST_CASE("destroyInstance releases the named instance's path claim") {
  clearSDK();
  const size_t before = cly::Countly::debugClaimedPathCount();
  cly::Countly &named = cly::Countly::createInstance("appPath");
  named.setHTTPClient(test_utils::fakeSendHTTP);
  named.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  named.SetPath("mi-named.db");
  named.start("APP_KEY_NAMED", COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(named.checkEQSize() == 0);
  REQUIRE(cly::Countly::debugClaimedPathCount() == before + 1);

  cly::Countly::destroyInstance("appPath");

  CHECK(cly::Countly::debugClaimedPathCount() == before);
  CHECK_FALSE(cly::Countly::hasInstance("appPath"));
  remove("mi-named.db");
}

TEST_CASE("start can be retried once a refused path is free") {
  clearSDK();
  std::shared_ptr<cly::Countly> b;
  {
    InstanceFixture a("APP_KEY_A", "mi-retry.db");
    REQUIRE(a.initialized());

    b = startInstanceAt("APP_KEY_B", "mi-retry.db");
    CHECK(b->checkEQSize() == -1);
  }
  // a is destroyed, so the path is free again.
  b->start("APP_KEY_B", COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  CHECK(b->checkEQSize() == 0);
  b.reset();
  remove("mi-retry.db");
}

#endif // COUNTLY_USE_SQLITE

// NOTE: keep this the LAST test case in this file. Releasing libcurl's global
// state cannot be undone for the process. Every other test drives HTTP through
// the fake client, so a released curl does not affect them.
TEST_CASE("shutdownNetworking releases once no instance is live") {
  cly::Countly::destroyAllInstances();
  REQUIRE(cly::Countly::debugLiveInstanceCount() == 0);

  cly::Countly::shutdownNetworking();

  CHECK(cly::RequestModule::globalNetworkingReleased() == true);
}
