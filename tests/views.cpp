#include "countly.hpp"
#include "doctest.h"

void validateViewSegmentation(nlohmann::json e, std::string name, std::string &viewId, bool isOpenView, bool isFirstView = false) {
  CHECK(e["key"].get<std::string>() == "[CLY]_view");
  CHECK(e["count"].get<int>() == 1);

  nlohmann::json s = e["segmentation"].get<nlohmann::json>();

  CHECK(s["_idv"].get<std::string>() == viewId);
  CHECK(s["name"].get<std::string>() == name);

  if (isOpenView) {
    CHECK(s["visit"].get<std::string>() == "1");
  }

  if (isFirstView) {
    CHECK(s["start"].get<std::string>() == "1");
  }
}

TEST_CASE("views are serialized correctly") {
  Countly &ct = Countly::getInstance();

  SUBCASE("views without segmentation") {
    SUBCASE("with name") {
      int eventSize = 0;
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::string eid = ct.views().openView("view1");
      eventSize++;

      ct.views().closeViewWithName("view1");
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, true, true);

      event = events.at(eventSize - 1);
      nlohmann::json c = nlohmann::json::parse(event);
      s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, false);
    }
    SUBCASE("with id") {
      int eventSize = ct.debugReturnStateOfEQ().size();

      std::string eid = ct.views().openView("view1");
      eventSize++;

      ct.views().closeViewWithID(eid);
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      nlohmann::json e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, true);

      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);

      validateViewSegmentation(e, "view1", eid, false);
    }
  }
  SUBCASE("views with segmentation") {
    SUBCASE("with name") {
      int eventSize = ct.debugReturnStateOfEQ().size();

      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
          {"name", "view2"},
      };
      std::string eid = ct.views().openView("view1", segmentation);
      eventSize++;

      ct.views().closeViewWithName("view2");
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view2", eid, true);
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view2", eid, false);
    }
    SUBCASE("with id") {
      int eventSize = ct.debugReturnStateOfEQ().size();

      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };
      std::string eid = ct.views().openView("view1", segmentation);
      eventSize++;

      ct.views().closeViewWithID(eid);
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      nlohmann::json e = nlohmann::json::parse(event);
      nlohmann::json s = e["segmentation"].get<nlohmann::json>();

      validateViewSegmentation(e, "view1", eid, true);
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      event = events.at(eventSize - 1);
      e = nlohmann::json::parse(event);
      validateViewSegmentation(e, "view1", eid, false);
    }
  }

  SUBCASE("CLOSING NONEXISTING VIEWS") {
    SUBCASE("with name") {
      int eventSize = ct.debugReturnStateOfEQ().size();
      ct.views().closeViewWithName("view1");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }

    SUBCASE("with id") {
      int eventSize = ct.debugReturnStateOfEQ().size();
      ct.views().closeViewWithName("event_id");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }
  }
}
