#include "doctest.h"

#include "countly/event.hpp"
#include "countly/internal_limits.hpp"
#include "nlohmann/json.hpp"

#include <map>
#include <string>

TEST_CASE("Internal Limits helpers") {
  SUBCASE("truncateString shorter than limit is unchanged") {
    CHECK(cly::limits::truncateString("hello", 10) == "hello");
    CHECK(cly::limits::truncateString("hello", 5) == "hello");
  }

  SUBCASE("truncateString longer than limit is cut") {
    CHECK(cly::limits::truncateString("hello world", 5) == "hello");
  }

  SUBCASE("truncateString does not split a UTF-8 multibyte char") {
    // bytes: 'a', 0xC3 0xA9 (e-acute), 0xC3 0xA9 (e-acute) => 5 bytes total
    std::string s;
    s += 'a';
    s += static_cast<char>(0xC3);
    s += static_cast<char>(0xA9);
    s += static_cast<char>(0xC3);
    s += static_cast<char>(0xA9);

    std::string expected; // 'a' + one e-acute = 3 bytes
    expected += 'a';
    expected += static_cast<char>(0xC3);
    expected += static_cast<char>(0xA9);

    // cut at 3 lands on a lead byte -> kept as-is
    CHECK(cly::limits::truncateString(s, 3) == expected);
    // cut at 4 lands mid-sequence -> backs off to 3
    CHECK(cly::limits::truncateString(s, 4) == expected);
  }

  SUBCASE("applySegmentationLimits truncates keys/values and caps count") {
    cly::SDKLimits lim{3, 3, 2, 100, 30, 200};
    std::map<std::string, std::string> in = {{"aaaa", "bbbb"}, {"cccc", "dddd"}, {"eeee", "ffff"}};
    std::map<std::string, std::string> out = cly::limits::applySegmentationLimits(in, lim);
    CHECK(out.size() == 2); // capped to maxSegmentationValues=2, first two in sorted order
    CHECK(out.count("aaa") == 1);
    CHECK(out["aaa"] == "bbb");
    CHECK(out.count("ccc") == 1);
    CHECK(out.count("eee") == 0);
  }

  SUBCASE("truncateStackTrace caps line count and line length") {
    std::string trace = "aaaaaa\nbbbbbb\ncccccc";
    CHECK(cly::limits::truncateStackTrace(trace, 2, 4) == "aaaa\nbbbb");
  }
}

TEST_CASE("Internal Limits Event applyLimits") {
  using json = nlohmann::json;

  SUBCASE("event key is truncated") {
    cly::Event e("abcdefghij", 1);
    e.applyLimits(5, 256, 100);
    CHECK(e.getKey() == "abcde");
  }

  SUBCASE("segmentation key and string value are truncated") {
    cly::Event e("k", 1);
    e.addSegmentation("longkey", "longvalue");
    e.applyLimits(4, 4, 100);
    json o = json::parse(e.serialize());
    CHECK(o["segmentation"].contains("long"));
    CHECK(o["segmentation"]["long"].get<std::string>() == "long");
  }

  SUBCASE("segmentation count is capped in sorted-key order") {
    cly::Event e("k", 1);
    e.addSegmentation("a", "1");
    e.addSegmentation("b", "2");
    e.addSegmentation("c", "3");
    e.addSegmentation("d", "4");
    e.applyLimits(128, 256, 2);
    json o = json::parse(e.serialize());
    CHECK(o["segmentation"].size() == 2);
    CHECK(o["segmentation"].contains("a"));
    CHECK(o["segmentation"].contains("b"));
  }

  SUBCASE("non-string segmentation value is left intact") {
    cly::Event e("k", 1);
    e.addSegmentation("n", 123456789);
    e.applyLimits(128, 2, 100);
    json o = json::parse(e.serialize());
    CHECK(o["segmentation"]["n"].is_number());
    CHECK(o["segmentation"]["n"].get<long long>() == 123456789);
  }
}
