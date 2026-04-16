#include "countly/event.hpp"
#include "doctest.h"
#include <ctime>
#include <iostream>
#include <string>
using namespace cly;

void valideEventParams(nlohmann::json eventJson, const std::string key, int count) {
  CHECK(eventJson["key"].get<std::string>() == key);
  CHECK(eventJson["count"].get<int>() == count);
  CHECK(std::to_string(eventJson["timestamp"].get<long long>()).size() == 13);
  CHECK(eventJson.contains("dow"));
  CHECK(eventJson.contains("hour"));
  int dow = eventJson["dow"].get<int>();
  int hour = eventJson["hour"].get<int>();
  CHECK(dow >= 0);
  CHECK(dow <= 6);
  CHECK(hour >= 0);
  CHECK(hour <= 23);
}

TEST_CASE("events are serialized correctly") {
  SUBCASE("without segmentation") {
    SUBCASE("without sum") {
      cly::Event event("win", 1);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "win", 1);
    }

    SUBCASE("with sum") {
      cly::Event event("buy", 2, 9.99);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["sum"].get<double>() == 9.99);
      valideEventParams(e, "buy", 2);
    }
  }

  SUBCASE("with segmentation") {
    SUBCASE("with signed integer") {
      cly::Event event("lose", 3);
      event.addSegmentation("points", -144);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "lose", 3);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["points"].get<int>() == -144);
    }

    SUBCASE("with unsigned integer") {
      cly::Event event("win", 1);
      event.addSegmentation("points", 232U);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "win", 1);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["points"].get<unsigned int>() == 232U);
    }

    SUBCASE("with boolean") {
      cly::Event event("win", 1);
      event.addSegmentation("alive", true);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "win", 1);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["alive"].get<bool>() == true);
    }

    SUBCASE("with string") {
      cly::Event event("message", 1);
      event.addSegmentation("sender", "TheLegend27");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "message", 1);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["sender"].get<std::string>() == "TheLegend27");
    }

    SUBCASE("with multiple values") {
      cly::Event event("buy", 5);
      event.addSegmentation("quantity", 27);
      event.addSegmentation("searchQuery", "cheap cheese");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "buy", 5);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["quantity"].get<int>() == 27);
      CHECK(s["searchQuery"].get<std::string>() == "cheap cheese");
    }

    SUBCASE("with multibyte strings") {
      cly::Event event("测试", 1);
      event.addSegmentation("苹果", "美味");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      valideEventParams(e, "测试", 1);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["苹果"].get<std::string>() == "美味");
    }
  }
}

TEST_CASE("events contain correct dow and hour") {
  SUBCASE("dow and hour match local time") {
    cly::Event event("test_time", 1);
    nlohmann::json e = nlohmann::json::parse(event.serialize());

    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);

    CHECK(e["dow"].get<int>() == local_tm.tm_wday);
    CHECK(e["hour"].get<int>() == local_tm.tm_hour);
  }

  SUBCASE("dow and hour are updated on startTimer") {
    cly::Event event("timed_event", 1);
    nlohmann::json before = nlohmann::json::parse(event.serialize());

    CHECK(before.contains("dow"));
    CHECK(before.contains("hour"));

    event.startTimer();
    nlohmann::json after = nlohmann::json::parse(event.serialize());

    // dow and hour should still be valid after startTimer resets timestamp
    int dow = after["dow"].get<int>();
    int hour = after["hour"].get<int>();
    CHECK(dow >= 0);
    CHECK(dow <= 6);
    CHECK(hour >= 0);
    CHECK(hour <= 23);
  }
}
