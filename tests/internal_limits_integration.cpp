#include <chrono>
#include <string>
#include <thread>

#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

// Initialize the SDK with a given SBS config, using the fake HTTP client and
// manual session control, then clear the queues so tests start clean.
static void limitsInit(const json &sbs, Countly &countly) {
  std::string s = sbs.dump();
  countly.setSDKBehaviorSettings(s);
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableManualSessionControl();
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  countly.processRQDebug();
  countly.clearRequestQueue();
  http_call_queue.clear();
}

static HTTPCall limitsPop() {
  CHECK(!http_call_queue.empty());
  HTTPCall c = http_call_queue.front();
  http_call_queue.pop_front();
  return c;
}

// Records one custom event, flushes, and returns its parsed JSON.
static json recordAndGetEvent(Countly &countly, cly::Event &e) {
  countly.addEvent(e);
  countly.processRQDebug();
  HTTPCall call = limitsPop();
  json events = json::parse(call.data["events"]);
  REQUIRE(events.size() == 1);
  return events[0];
}

TEST_CASE("Internal Limits Events") {
  clearSDK();
  http_call_queue.clear();

  SUBCASE("SBS lkl truncates event key") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 5}, {"eqs", 1}}, countly);

    cly::Event e("abcdefghij", 1);
    json ev = recordAndGetEvent(countly, e);
    CHECK(ev["key"].get<std::string>() == "abcde");
  }

  SUBCASE("SBS lvs/lkl truncate segmentation key and value") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 5}, {"lvs", 4}, {"eqs", 1}}, countly);

    cly::Event e("evt", 1);
    e.addSegmentation("longkey123", "longvalue");
    json ev = recordAndGetEvent(countly, e);
    CHECK(ev["segmentation"].contains("longk"));
    CHECK(ev["segmentation"]["longk"].get<std::string>() == "long");
  }

  SUBCASE("SBS lsv caps segmentation entry count") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lsv", 2}, {"eqs", 1}}, countly);

    cly::Event e("evt", 1);
    e.addSegmentation("a", "1");
    e.addSegmentation("b", "2");
    e.addSegmentation("c", "3");
    e.addSegmentation("d", "4");
    json ev = recordAndGetEvent(countly, e);
    CHECK(ev["segmentation"].size() == 2);
    CHECK(ev["segmentation"].contains("a"));
    CHECK(ev["segmentation"].contains("b"));
  }

  SUBCASE("default limit truncates long key when no SBS override") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"eqs", 1}}, countly); // no lkl -> default 128

    cly::Event e(std::string(200, 'x'), 1);
    json ev = recordAndGetEvent(countly, e);
    CHECK(ev["key"].get<std::string>().size() == 128);
  }

  SUBCASE("internal view event key is not truncated by event limits") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 3}, {"eqs", 1}}, countly);

    std::string viewId = countly.views().openView("home");
    CHECK(!viewId.empty());
    countly.processRQDebug();
    HTTPCall call = limitsPop();
    json events = json::parse(call.data["events"]);
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "[CLY]_view"); // not "[CL"
  }
}

TEST_CASE("Internal Limits Views") {
  clearSDK();
  http_call_queue.clear();

  // Helper: open a view and return the parsed [CLY]_view event.
  auto openAndGetViewEvent = [](Countly &countly, const std::string &name, const std::map<std::string, std::string> &seg) -> json {
    countly.views().openView(name, seg);
    countly.processRQDebug();
    HTTPCall call = limitsPop();
    json events = json::parse(call.data["events"]);
    REQUIRE(events.size() == 1);
    return events[0];
  };

  SUBCASE("view name is truncated to maxKeyLength via SBS lkl") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 5}, {"vt", true}, {"eqs", 1}}, countly);

    json ev = openAndGetViewEvent(countly, "abcdefghij", {});
    CHECK(ev["segmentation"]["name"].get<std::string>() == "abcde");
  }

  SUBCASE("developer view segmentation is limited; reserved keys are untouched") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 3}, {"lvs", 3}, {"lsv", 2}, {"vt", true}, {"eqs", 1}}, countly);

    json ev = openAndGetViewEvent(countly, "homeview", {{"aaaa", "bbbb"}, {"cccc", "dddd"}, {"eeee", "ffff"}});
    json seg = ev["segmentation"];

    // reserved keys present and untouched
    CHECK(seg["visit"].get<std::string>() == "1");
    CHECK(seg.contains("_idv"));
    CHECK(seg.contains("name")); // internal key name kept

    // developer entries: truncated keys/values, capped to 2
    int devCount = 0;
    for (auto it = seg.begin(); it != seg.end(); ++it) {
      const std::string &k = it.key();
      if (k == "visit" || k == "start" || k == "_idv" || k == "name") {
        continue;
      }
      devCount++;
      CHECK(k.size() <= 3);
      CHECK(it.value().get<std::string>().size() <= 3);
    }
    CHECK(devCount == 2);
  }
}

TEST_CASE("Internal Limits Crash") {
  clearSDK();
  http_call_queue.clear();

  auto recordAndGetCrash = [](Countly &countly) -> json {
    countly.processRQDebug();
    HTTPCall call = limitsPop();
    REQUIRE(call.data.find("crash") != call.data.end());
    return json::parse(call.data["crash"]);
  };

  SUBCASE("crash title is truncated to maxStackTraceLineLength (ltl)") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"ltl", 5}, {"crt", true}}, countly);

    countly.crash().recordException("abcdefghij", "line", false, {{"_os", "OS"}, {"_app_version", "1"}}, {});
    json crash = recordAndGetCrash(countly);
    CHECK(crash["_name"].get<std::string>() == "abcde");
  }

  SUBCASE("stack trace line length and line count are capped") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"ltl", 4}, {"ltlpt", 2}, {"crt", true}}, countly);

    countly.crash().recordException("t", "aaaaaa\nbbbbbb\ncccccc", false, {{"_os", "OS"}, {"_app_version", "1"}}, {});
    json crash = recordAndGetCrash(countly);
    CHECK(crash["_error"].get<std::string>() == "aaaa\nbbbb");
  }

  SUBCASE("breadcrumb text is truncated and count capped (lvs + lbc)") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lvs", 3}, {"lbc", 2}, {"crt", true}}, countly);

    countly.crash().addBreadcrumb("aaaaaa");
    countly.crash().addBreadcrumb("bbbbbb");
    countly.crash().addBreadcrumb("cccccc");
    countly.crash().recordException("t", "trace", false, {{"_os", "OS"}, {"_app_version", "1"}}, {});
    json crash = recordAndGetCrash(countly);
    // oldest ("aaa") dropped by lbc=2; each truncated to 3 chars; newline-joined
    CHECK(crash["_logs"].get<std::string>() == "bbb\nccc\n");
  }

  SUBCASE("crash segmentation is limited but crash metrics are untouched") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 3}, {"lvs", 3}, {"lsv", 2}, {"crt", true}}, countly);

    countly.crash().recordException("t", "trace", false,
                                    {{"_os", "AVeryLongOperatingSystemName"}, {"_app_version", "1"}},
                                    {{"aaaa", "bbbb"}, {"cccc", "dddd"}, {"eeee", "ffff"}});
    json crash = recordAndGetCrash(countly);

    // metrics untouched
    CHECK(crash["_os"].get<std::string>() == "AVeryLongOperatingSystemName");

    // segmentation limited: 2 entries, keys/values <= 3 chars
    json custom = crash["_custom"];
    CHECK(custom.size() == 2);
    for (auto it = custom.begin(); it != custom.end(); ++it) {
      CHECK(it.key().size() <= 3);
      CHECK(it.value().get<std::string>().size() <= 3);
    }
  }
}

TEST_CASE("Internal Limits User Details") {
  clearSDK();
  http_call_queue.clear();

  auto getUserDetails = [](Countly &countly) -> json {
    countly.processRQDebug();
    HTTPCall call = limitsPop();
    REQUIRE(call.data.find("user_details") != call.data.end());
    return json::parse(call.data["user_details"]);
  };

  SUBCASE("named user detail value is truncated to maxValueSize") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lvs", 3}}, countly);

    countly.setUserDetails({{"name", "abcdef"}});
    json ud = getUserDetails(countly);
    CHECK(ud["name"].get<std::string>() == "abc");
  }

  SUBCASE("picture field is allowed up to 4096 and capped beyond it") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lvs", 3}}, countly);

    countly.setUserDetails({{"picture", std::string(5000, 'x')}});
    json ud = getUserDetails(countly);
    CHECK(ud["picture"].get<std::string>().size() == 4096);
  }

  SUBCASE("custom user detail key and value are truncated; no count cap") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    limitsInit({{"lkl", 3}, {"lvs", 3}, {"lsv", 1}}, countly);

    countly.setCustomUserDetails({{"longkey", "longval"}, {"another", "value2"}, {"third", "value3"}});
    json ud = getUserDetails(countly);
    json custom = ud["custom"];
    // lsv does NOT cap user properties -> all 3 kept
    CHECK(custom.size() == 3);
    CHECK(custom.contains("lon"));
    CHECK(custom["lon"].get<std::string>() == "lon");
  }
}

TEST_CASE("Internal Limits Setters") {
  SUBCASE("setters write config fields before init") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setMaxKeyLength(50);
    countly.setMaxValueSize(60);
    countly.setMaxSegmentationValues(7);
    countly.setMaxBreadcrumbCount(9);
    countly.setMaxStackTraceLinesPerThread(11);
    countly.setMaxStackTraceLineLength(13);

    const CountlyConfiguration &cfg = countly.getConfiguration();
    CHECK(cfg.maxKeyLength == 50);
    CHECK(cfg.maxValueSize == 60);
    CHECK(cfg.maxSegmentationValues == 7);
    CHECK(cfg.breadcrumbsThreshold == 9);
    CHECK(cfg.maxStackTraceLinesPerThread == 11);
    CHECK(cfg.maxStackTraceLineLength == 13);
  }

  SUBCASE("setter is ignored after initialization") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setMaxKeyLength(50);
    limitsInit({{"eqs", 1}}, countly); // SDK now initialized

    countly.setMaxKeyLength(7); // should be a no-op post-init
    CHECK(countly.getConfiguration().maxKeyLength == 50);
  }

  SUBCASE("developer-set default limit is enforced when no SBS override") {
    clearSDK();
    Countly &countly = Countly::getInstance();
    countly.setMaxKeyLength(4);
    limitsInit({{"eqs", 1}}, countly); // no lkl in SBS -> uses config default 4

    cly::Event e("abcdef", 1);
    countly.addEvent(e);
    countly.processRQDebug();
    HTTPCall call = limitsPop();
    json events = json::parse(call.data["events"]);
    REQUIRE(events.size() == 1);
    CHECK(events[0]["key"].get<std::string>() == "abcd");
  }
}
