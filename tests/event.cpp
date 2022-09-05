#include "countly/event.hpp"
#include "doctest.h"
#include <iostream>
#include <string>
using namespace cly;

TEST_CASE("events are serialized correctly") {
  SUBCASE("without segmentation") {
    SUBCASE("without sum") {
      cly::Event event("win", 1);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "win");
      CHECK(e["count"].get<int>() == 1);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);
    }

    SUBCASE("with sum") {
      cly::Event event("buy", 2, 9.99);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "buy");
      CHECK(e["count"].get<int>() == 2);
      CHECK(e["sum"].get<double>() == 9.99);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);
    }
  }

  SUBCASE("with segmentation") {
    SUBCASE("with signed integer") {
      cly::Event event("lose", 3);
      event.addSegmentation("points", -144);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "lose");
      CHECK(e["count"].get<int>() == 3);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["points"].get<int>() == -144);
    }

    SUBCASE("with unsigned integer") {
      cly::Event event("win", 1);
      event.addSegmentation("points", 232U);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "win");
      CHECK(e["count"].get<int>() == 1);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["points"].get<unsigned int>() == 232U);
    }

    SUBCASE("with boolean") {
      cly::Event event("win", 1);
      event.addSegmentation("alive", true);

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "win");
      CHECK(e["count"].get<int>() == 1);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["alive"].get<bool>() == true);
    }

    SUBCASE("with string") {
      cly::Event event("message", 1);
      event.addSegmentation("sender", "TheLegend27");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "message");
      CHECK(e["count"].get<int>() == 1);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["sender"].get<std::string>() == "TheLegend27");
    }

    SUBCASE("with multiple values") {
      cly::Event event("buy", 5);
      event.addSegmentation("quantity", 27);
      event.addSegmentation("searchQuery", "cheap cheese");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "buy");
      CHECK(e["count"].get<int>() == 5);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["quantity"].get<int>() == 27);
      CHECK(s["searchQuery"].get<std::string>() == "cheap cheese");
    }

    SUBCASE("with multibyte strings") {
      cly::Event event("测试", 1);
      event.addSegmentation("苹果", "美味");

      nlohmann::json e = nlohmann::json::parse(event.serialize());
      CHECK(e["key"].get<std::string>() == "测试");
      CHECK(e["count"].get<int>() == 1);
      CHECK(std::to_string(e["timestamp"].get<long long>()).size() == 13);

      nlohmann::json s = e["segmentation"].get<nlohmann::json>();
      CHECK(s["苹果"].get<std::string>() == "美味");
    }
  }
}
