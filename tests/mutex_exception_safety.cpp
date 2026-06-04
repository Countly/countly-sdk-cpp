#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <future>
#include <thread>

using namespace cly;
using namespace test_utils;

/**
 * Regression tests for mutex exception-safety in the background update loop.
 *
 * Before the RAII conversion, an exception thrown WHILE the shared mutex was
 * held (e.g. nlohmann::json::parse on a malformed event inside updateSession)
 * skipped the trailing mutex->unlock(): the background thread returned still
 * owning the mutex, so the subsequent stop() -> _deleteThread() blocked forever
 * on its own lock_guard -> shutdown deadlock.
 *
 * These tests assert that stop() stays responsive after an exception is raised
 * inside the running loop. stop() is wrapped in std::async + wait_for so a
 * regression manifests as a clean test failure within the timeout instead of
 * hanging the whole test binary (which would also wedge the next test's
 * clearSDK()/halt()).
 */

// The malformed-event vector exercises the non-SQLite (in-memory) event queue
// path in updateSession(); the parse-while-locked site (json::parse over the
// in-memory queue) is compiled out under COUNTLY_USE_SQLITE.
#ifndef COUNTLY_USE_SQLITE
TEST_CASE("mutex exception safety - malformed event in updateSession does not deadlock stop()") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  // Short interval so the threaded loop reaches updateSession() quickly.
  ct.setUpdateInterval(50);
  http_call_queue.clear();

  // start_thread=true => automatic session + background updateLoop running.
  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);
  // Let the loop run a couple of cycles so the session has begun.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Inject a string that is NOT valid JSON. On the next cycle, updateSession()
  // iterates the event queue and calls nlohmann::json::parse on it WHILE holding
  // the mutex, which throws. With RAII the unique_lock releases on unwind; the
  // exception reaches updateLoop's catch, the loop ends, and the thread returns
  // WITHOUT owning the mutex.
  ct.debugInjectRawEvent("{not valid json");

  // Give the loop time to pick up the bad event and throw.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // stop() must return. Run it on another thread so a deadlock fails the test
  // within the timeout rather than hanging the suite.
  auto fut = std::async(std::launch::async, [&]() { ct.stop(); });
  bool returned = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  REQUIRE(returned);
  fut.get();
}
#endif

// Exercises a USER-THREAD leak path: checkAndSendEventToRQ() parses the in-memory
// event queue while holding the mutex. A malformed entry makes nlohmann::json::parse
// throw there. Pre-fix the bare lock was leaked; post-fix the unique_lock releases on
// unwind. We assert a SUBSEQUENT locking call still returns (i.e. the mutex was freed),
// which would hang forever if the lock had leaked.
#ifndef COUNTLY_USE_SQLITE
TEST_CASE("mutex exception safety - throwing user-API path does not leak the mutex") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(fakeSendHTTP);
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.setEventsToRQThreshold(1); // low threshold so addEvent triggers checkAndSendEventToRQ
  http_call_queue.clear();
  // start_thread=false: pure user-thread test, no background loop involved.
  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

  // Seed a malformed event, then add a valid one. addEvent -> checkAndSendEventToRQ
  // iterates the queue (size >= threshold) and json::parse throws while the mutex is held.
  ct.debugInjectRawEvent("{not valid json");
  bool threw = false;
  try {
    cly::Event ev("trigger", 1);
    ct.addEvent(ev);
  } catch (...) {
    threw = true; // exception propagating out is expected; the point is the mutex is released
  }

  // If the mutex had leaked, this locking call would block forever.
  auto fut = std::async(std::launch::async, [&]() { return ct.checkEQSize(); });
  bool returned = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  REQUIRE(returned);
  fut.get();
  (void)threw;
}
#endif

// This vector works in every build: a throwing HTTP client raises an exception
// inside the running loop (from sendHTTP / processQueue). It does not reproduce
// the original lock-leak deadlock (sendHTTP runs with the mutex released), but
// it guards the end-to-end invariant that an exception in the loop never wedges
// shutdown, and that the request-queue processing flag is cleared on unwind so
// processing can resume.
TEST_CASE("mutex exception safety - throwing HTTP client keeps stop() responsive") {
  clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient([](bool, const std::string &, const std::string &) -> HTTPResponse {
    throw std::runtime_error("simulated HTTP failure");
  });
  ct.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.setUpdateInterval(50);
  http_call_queue.clear();

  ct.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, true);
  // Let at least one loop cycle reach sendHTTP and throw.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  auto fut = std::async(std::launch::async, [&]() { ct.stop(); });
  bool returned = fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  REQUIRE(returned);
  fut.get();
}
