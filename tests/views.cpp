#include "countly.hpp"
#include "doctest.h"

//{\"count\":3,\"key\":\"lose\",\"segmentation\":{\"points\":-144}}

TEST_CASE("events are serialized correctly") {
  SUBCASE("open view") {
    SUBCASE("without segmentation") {
      Countly &ct = Countly::getInstance();
      ct.views().openView("view1");
      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      std::string event = events.front();
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      
      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() != "");
      CHECK(s["start"].get<std::string>() == "1");
      CHECK(s["name"].get<std::string>() == "view1");
      
    }

    SUBCASE("with segmentation") {

	    
      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };

      Countly &ct = Countly::getInstance();
      std::vector<std::string> events = ct.debugReturnStateOfEQ();
      events.clear();

      ct.views().openView("view2", segmentation);
      events = ct.debugReturnStateOfEQ();
      
      std::string event = events.at(0);
      json e = json::parse(event);
      json s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view2");
      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() != "");
      CHECK(s["start"].get<std::string>() == "1");
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");

      segmentation.clear();
      segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };
      ct.views().openView("view3", segmentation);
      events = ct.debugReturnStateOfEQ();
      event = events.at(1);
      e = json::parse(event);
      s = e["segmentation"].get<json>();

      CHECK(e["key"].get<std::string>() == "[CLY]_view");
      CHECK(e["count"].get<int>() == 1);

      CHECK(s["name"].get<std::string>() == "view3");
      CHECK(s["visit"].get<std::string>() == "1");
      CHECK(s["_idv"].get<std::string>() != "");
      CHECK(s["start"].get<std::string>() == "0");
      CHECK(s["platform"].get<std::string>() == "ubuntu");
      CHECK(s["time"].get<std::string>() == "60");
    }
  }

  SUBCASE("close view") {
    SUBCASE("without segmentation") {
      Countly &ct = Countly::getInstance();
      ct.views().closeViewWithName("view1");
    //  CHECK(event.serialize() == "{\"count\":1,\"key\":\"win\"}");
    }

    SUBCASE("with segmentation") {
      Countly &ct = Countly::getInstance();
      ct.views().closeViewWithName("view2");
    }
  }
}
