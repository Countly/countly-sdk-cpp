#include "countly.hpp"
#include "doctest.h"

TEST_CASE("events are serialized correctly") {
	SUBCASE("without segmentation") {
		SUBCASE("without sum") {
			Countly::Event event("win", 1);
			CHECK(event.serialize() == "{\"count\":1,\"key\":\"win\"}");
		}

		SUBCASE("with sum") {
			Countly::Event event("buy", 2, 9.99);
			CHECK(event.serialize() == "{\"count\":2,\"key\":\"buy\",\"sum\":9.99}");
		}
	}

	SUBCASE("with segmentation") {
		SUBCASE("with signed integer") {
			Countly::Event event("lose", 3);
			event.addSegmentation("points", -144);
			CHECK(event.serialize() == "{\"count\":3,\"key\":\"lose\",\"segmentation\":{\"points\":-144}}");
		}

		SUBCASE("with unsigned integer") {
			Countly::Event event("win", 1);
			event.addSegmentation("points", 232U);
			CHECK(event.serialize() == "{\"count\":1,\"key\":\"win\",\"segmentation\":{\"points\":232}}");
		}

		SUBCASE("with boolean") {
			Countly::Event event("win", 1);
			event.addSegmentation("alive", true);
			CHECK(event.serialize() == "{\"count\":1,\"key\":\"win\",\"segmentation\":{\"alive\":true}}");
		}

		SUBCASE("with string") {
			Countly::Event event("message", 1);
			event.addSegmentation("sender", "TheLegend27");
			CHECK(event.serialize() == "{\"count\":1,\"key\":\"message\",\"segmentation\":{\"sender\":\"TheLegend27\"}}");
		}

		SUBCASE("with multiple values") {
			Countly::Event event("buy", 5);
			event.addSegmentation("quantity", 27);
			event.addSegmentation("searchQuery", "cheap cheese");
			CHECK(event.serialize() == "{\"count\":5,\"key\":\"buy\",\"segmentation\":{\"quantity\":27,\"searchQuery\":\"cheap cheese\"}}");
		}

		SUBCASE("with changing values") {
			Countly::Event event("lose", 3);
			event.addSegmentation("points", -144);
			event.addSegmentation("points", 2000);
			CHECK(event.serialize() == "{\"count\":3,\"key\":\"lose\",\"segmentation\":{\"points\":2000}}");
		}
	}
}
