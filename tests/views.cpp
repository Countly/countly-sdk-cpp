#include "countly.hpp"
#include "doctest.h"

TEST_CASE("views are serialized correctly") {
  Countly &ct = Countly::getInstance();
  static int eventSize = 0;

  SUBCASE("views without segmentation") {
    SUBCASE("with name") {
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::string eid = ct.views().openView("view1");
      eventSize++;

      ct.views().closeViewWithName("view1");
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() == eid);
      CHECK(s["start"].get<std::string>() == "1");
      CHECK(s["name"].get<std::string>() == "view1");

      event = events.at(eventSize - 1);
      json c = json::parse(event);
      s = e["segmentation"].get<json>();

      CHECK(c["key"].get<std::string>() == "[CLY]_view");
      CHECK(c["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view1");
      CHECK(s["_idv"].get<std::string>() != "");
    }
    SUBCASE("with id") {
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

      std::string eid = ct.views().openView("view1");
      eventSize++;

      ct.views().closeViewWithID(eid);
      eventSize++;

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      CHECK(events.size() == eventSize);

      std::string event = events.at(eventSize - 2);
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() == eid);
      CHECK(s["start"].get<std::string>() == "0");
      CHECK(s["name"].get<std::string>() == "view1");

      event = events.at(eventSize - 1);
      json c = json::parse(event);
      s = e["segmentation"].get<json>();

      CHECK(c["key"].get<std::string>() == "[CLY]_view");
      CHECK(c["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view1");
      CHECK(s["_idv"].get<std::string>() != "");
    }
  }
  SUBCASE("views with segmentation") {
    SUBCASE("with name") {
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

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
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() == eid);
      CHECK(s["start"].get<std::string>() == "0");
      CHECK(s["name"].get<std::string>() == "view2");
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      event = events.at(eventSize - 1);
      json c = json::parse(event);
      s = e["segmentation"].get<json>();

      CHECK(c["key"].get<std::string>() == "[CLY]_view");
      CHECK(c["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view2");
      CHECK(s["_idv"].get<std::string>() != "");
    }
    SUBCASE("with id") {
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);

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
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() == eid);
      CHECK(s["start"].get<std::string>() == "0");
      CHECK(s["name"].get<std::string>() == "view1");
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      event = events.at(eventSize - 1);
      json c = json::parse(event);
      s = e["segmentation"].get<json>();

      CHECK(c["key"].get<std::string>() == "[CLY]_view");
      CHECK(c["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view1");
      CHECK(s["_idv"].get<std::string>() != "");
    }
  }

  SUBCASE("CLOSING NONEXISTING VIEWS") {
    SUBCASE("with name") {
	  CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
      ct.views().closeViewWithName("view1");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }

    SUBCASE("with id") {
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
      ct.views().closeViewWithName("event_id");
      CHECK(ct.debugReturnStateOfEQ().size() == eventSize);
    }
  }
}
