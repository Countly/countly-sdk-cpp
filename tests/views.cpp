#include "countly.hpp"
#include "doctest.h"
#include "test_utils.hpp"

#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace cly;
using namespace std::literals::chrono_literals;

/**
 * Validate view data.
 *
 * @param e: json holding view data.
 * @param name: name of the view.
 * @param viewId: id of the view.
 * @param duration: duration of the view.
 * @param isOpenView: set 'true' if it is a open view.
 * @param isFirstView: set 'true' if it is very first view.
 */
void validateViewSegmentation(nlohmann::json e, const std::string name, std::string &viewId, double duration, bool isOpenView, bool isFirstView = false) {
  CHECK(e["key"].get<std::string>() == "[CLY]_view");
  CHECK(e["count"].get<int>() == 1);
  CHECK(e["dur"].get<double>() >= duration);

  nlohmann::json s = e["segmentation"].get<nlohmann::json>();

  CHECK(s["_idv"].get<std::string>() == viewId);
  CHECK(s["name"].get<std::string>() == name);

  if (isOpenView) {
    CHECK(s["visit"].get<std::string>() == "1");
  } else {
    CHECK(s.contains("visit") == false);
  }

  if (isFirstView) {
    CHECK(s["start"].get<std::string>() == "1");
  } else {
    CHECK(s.contains("start") == false);
  }
}

TEST_CASE("recording views") {
  test_utils::clearSDK();
  Countly &ct = Countly::getInstance();
  ct.SetPath(TEST_DATABASE_NAME);
  ct.setDeviceID("test-device-id");
  ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);

  /*
   * It validates the views recorded without segmentation.
   */
  SUBCASE("views without segmentation") {
    /*
     * Case: Open a view without segmentation and close it with the name.
     */
    SUBCASE("with name") {
      unsigned int eventSize = 0;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::string eid = ct.views().openView("view1");
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      validateViewSegmentation(e, "view1", eid, 0, true, true);

      std::this_thread::sleep_for(3s);
      ct.views().closeViewWithName("view1");
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, 3, false, false);
    }

    /*
     * Open a view without segmentation and close it the id.
     */
    SUBCASE("with id") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();

      std::string eid = ct.views().openView("view1");
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, 0, true, true);

      std::this_thread::sleep_for(2s);

      ct.views().closeViewWithID(eid);
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, 2, false);
    }
  }

  /*
   * It validates the views recorded with segmentation.
   */
  SUBCASE("views with segmentation") {
    /*
     * Case: Open a view with segmentation and close it with the name.
     */
    SUBCASE("with name") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();

      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
          {"name", "xxxxxxx"},
      };
      std::string eid = ct.views().openView("view2", segmentation);
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::vector<std::string> events = ct.debugReturnStateOfEQ();

      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view2", eid, 0, true, true);

      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      std::this_thread::sleep_for(3s);

      ct.views().closeViewWithName("view2");
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view2", eid, 3, false);
    }
    /*
     * Case: Open a view without segmentation and close it with the id.
     */
    SUBCASE("with id") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();

      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };
      std::string eid = ct.views().openView("view1", segmentation);
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::vector<std::string> events = ct.debugReturnStateOfEQ();

      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, 0, true, true);
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      std::this_thread::sleep_for(1s);

      ct.views().closeViewWithID(eid);
      eventSize++;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view1", eid, 1, false);
    }
  }

  /*
   * It validates the event queue when closing non-existing views.
   */
  SUBCASE("CLOSING NONEXISTING VIEWS") {
    /*
     * Closing non-existing view with name.
     */
    SUBCASE("with name") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();
      ct.views().closeViewWithName("view1");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }

    /*
     * Closing non-existing view with id.
     */
    SUBCASE("with id") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();
      ct.views().closeViewWithName("event_id");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }
  }
}

/**
 * Views used to be opened and closed without any lock of their own, so two
 * threads touching the same instance raced on the open-view map (and on the
 * first-view flag). Asserts are collected here and checked on the main thread.
 */
TEST_CASE("views opened and closed from several threads at once") {
  test_utils::clearSDK();
  Countly &ct = Countly::getInstance();
  ct.setHTTPClient(test_utils::fakeSendHTTP);
  ct.SetPath(TEST_DATABASE_NAME);
  ct.setDeviceID("test-device-id");
  ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, false);
  // Keep every view event in the event queue so all of them can be counted.
  ct.setEventsToRQThreshold(10000);
  REQUIRE(ct.debugReturnStateOfEQ().size() == 0);

  const int thread_count = 4;
  const int views_per_thread = 20;

  std::mutex ids_mutex;
  std::vector<std::string> view_ids;
  std::vector<std::thread> workers;

  for (int t = 0; t < thread_count; t++) {
    workers.emplace_back([&ct, &ids_mutex, &view_ids, t]() {
      for (int i = 0; i < views_per_thread; i++) {
        const std::string viewId = ct.views().openView("view " + std::to_string(t));
        {
          std::lock_guard<std::mutex> lk(ids_mutex);
          view_ids.push_back(viewId);
        }
        ct.views().closeViewWithID(viewId);
      }
    });
  }
  for (std::thread &worker : workers) {
    worker.join();
  }

  // Every open handed back a distinct, non-empty id.
  CHECK(view_ids.size() == thread_count * views_per_thread);
  std::set<std::string> unique_ids(view_ids.begin(), view_ids.end());
  CHECK(unique_ids.size() == view_ids.size());
  CHECK(unique_ids.count("") == 0);

  // Every pair recorded exactly two view events, and 'start' was claimed once.
  int view_events = 0;
  int start_events = 0;
  for (const std::string &raw : ct.debugReturnStateOfEQ()) {
    nlohmann::json e = nlohmann::json::parse(raw);
    if (e["key"].get<std::string>() != "[CLY]_view") {
      continue;
    }
    view_events++;
    if (e["segmentation"].contains("start")) {
      start_events++;
    }
  }
  CHECK(view_events == 2 * thread_count * views_per_thread);
  CHECK(start_events == 1);
}
