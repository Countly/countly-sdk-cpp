#include "countly.hpp"
#include "doctest.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

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
void validateViewSegmentation(nlohmann::json e, std::string name, std::string &viewId, double duration, bool isOpenView, bool isFirstView = false) {
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
  Countly &ct = Countly::getInstance();

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

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      validateViewSegmentation(e, "view1", eid, 0, true, true);

      Sleep(3000);
      ct.views().closeViewWithName("view1");
      eventSize++;

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, 3, false);

      CHECK(events.size() == eventSize);
    }

    /*
    * Open a view without segmentation and close it the id.
    */
    SUBCASE("with id") {
      unsigned int eventSize = ct.debugReturnStateOfEQ().size();

      std::string eid = ct.views().openView("view1");
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, 0, true);

      Sleep(2000);

      ct.views().closeViewWithID(eid);
      eventSize++;

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, 2, false);
      CHECK(events.size() == eventSize);
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
          {"name", "view2"},
      };
      std::string eid = ct.views().openView("view1", segmentation);
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();

      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view2", eid, 0, true);

      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      Sleep(3000);

      ct.views().closeViewWithName("view2");
      eventSize++;

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view2", eid, 3, false);

      CHECK(events.size() == eventSize);
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

      std::vector<std::string> events = ct.debugReturnStateOfEQ();

      std::string event = events.at(eventSize - 1);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, 0, true);
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      Sleep(1000);

      ct.views().closeViewWithID(eid);
      eventSize++;

      events = ct.debugReturnStateOfEQ();
      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view1", eid, 1, false);

      CHECK(events.size() == eventSize);
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
