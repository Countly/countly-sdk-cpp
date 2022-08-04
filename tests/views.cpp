#include "countly.hpp"
#include "doctest.h"

TEST_CASE("views are serialized correctly") {
  Countly &ct = Countly::getInstance();
  SUBCASE("open view") {
    SUBCASE("without segmentation") {
      std::string eid = ct.views().openView("view1");
      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.front();
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() == eid);
      CHECK(s["start"].get<std::string>() == "1");
      CHECK(s["name"].get<std::string>() == "view1");
    }

    SUBCASE("with segmentation") {
      SUBCASE("validate 'start' field in segmentation") {
        std::map<std::string, std::string> segmentation = {
            {"platform", "ubuntu"},
            {"time", "60"},
        };

        std::string eid = ct.views().openView("view2", segmentation);
        std::vector<std::string> events = ct.debugReturnStateOfEQ();

        std::string event = events.at(1);
        json e = json::parse(event);
        json s = e["segmentation"].get<json>();

        CHECK(e["key"].get<std::string>() == "[CLY]_view");
        CHECK(e["count"].get<int>() == 1);

        CHECK(s["name"].get<std::string>() == "view2");
        CHECK(s["visit"].get<std::string>() == "1");
        CHECK(s["_idv"].get<std::string>() == eid);
        CHECK(s["start"].get<std::string>() == "0");
        CHECK(s["platform"].get<std::string>() == "ubuntu");
        CHECK(s["time"].get<std::string>() == "60");
      }

      SUBCASE("override view name") {
        std::map<std::string, std::string> segmentation = {
            {"platform", "ubuntu"},
            {"time", "60"},
            {"name", "view4"},

        };

        std::string eid = ct.views().openView("view3", segmentation);
        std::vector<std::string> events = ct.debugReturnStateOfEQ();

        std::string event = events.at(2);
        json e = json::parse(event);
        json s = e["segmentation"].get<json>();

        CHECK(e["key"].get<std::string>() == "[CLY]_view");
        CHECK(e["count"].get<int>() == 1);

        CHECK(s["name"].get<std::string>() == "view4");
        CHECK(s["visit"].get<std::string>() == "1");
        CHECK(s["_idv"].get<std::string>() == eid);
        CHECK(s["start"].get<std::string>() == "0");
        CHECK(s["platform"].get<std::string>() == "ubuntu");
        CHECK(s["time"].get<std::string>() == "60");
      }
    }
  }

  SUBCASE("close view") {
    SUBCASE("close view with name") {
      ct.views().closeViewWithName("view1");

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      events = ct.debugReturnStateOfEQ();

      std::string event = events.at(3);
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view1");
      CHECK(s["_idv"].get<std::string>() != "");
    }

    SUBCASE("close view with id") {
      std::string eid = ct.views().openView("view-test");
      ct.views().closeViewWithID(eid);

      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      events = ct.debugReturnStateOfEQ();

      std::string event = events.at(5);
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view-test");
      CHECK(s["_idv"].get<std::string>() == eid);
    }
  }
}
