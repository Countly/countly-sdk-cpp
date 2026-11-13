// In each request scenario the SDK sends the requests api/requests/<scenario>.json records, with the values that change
// from run to run masked. The scenarios make the calls the other Countly SDKs make in their own request tests. A
// deliberate change: run the tests with COUNTLY_UPDATE_REQUESTS=1, for example
// COUNTLY_UPDATE_REQUESTS=1 ./build/countly-tests -tc="request scenario*", which writes the records, check the
// difference and add a line to CHANGELOG.md.
#include "countly.hpp"
#include "doctest.h"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace cly;

namespace {
const char *const MASK = "<masked>";
const std::set<std::string> MASKED_PARAMS = {"timestamp", "hour", "dow", "tz", "sdk_version", "av", "rr", "checksum", "checksum256", "session_duration"};
const std::set<std::string> JSON_PARAMS = {"events", "metrics", "user_details", "crash", "consent", "hc"};
const std::set<std::string> MASKED_EVENT_FIELDS = {"timestamp", "hour", "dow", "id", "pvid", "cvid", "peid"};
const std::set<std::string> MASKED_CRASH_FIELDS = {"_error", "_run", "_os", "_os_version", "_resolution", "_density", "_locale", "_device", "_device_type", "_manufacturer", "_cpu", "_opengl", "_ram_current", "_ram_total", "_disk_current", "_disk_total", "_bat", "_orientation", "_root", "_online", "_muted", "_background"};
// a view's "_idv" is the ID the SDK generates for it
const std::set<std::string> MASKED_VIEW_SEGMENTS = {"_idv"};
const int MAX_INT = 2147483647;
const std::string RECORDS = std::string(COUNTLY_SOURCE_DIR) + "/api/requests/";

std::mutex sent_mutex;
std::vector<nlohmann::ordered_json> sent;

/**
 * A copy of an object with the values of the given keys masked.
 *
 * @param value: object
 * @param keys: keys to mask, or nullptr to mask every key
 * @return the masked copy
 */
nlohmann::json masked(const nlohmann::json &value, const std::set<std::string> *keys) {
  nlohmann::json copy = nlohmann::json::object();
  for (const auto &item : value.items()) {
    copy[item.key()] = (keys == nullptr || keys->count(item.key()) > 0) ? nlohmann::json(MASK) : item.value();
  }
  return copy;
}

/**
 * The value of a JSON parameter with the values that change from run to run masked.
 *
 * @param name: parameter name
 * @param value: parsed parameter value
 * @return the masked value
 */
nlohmann::json maskedJson(const std::string &name, const nlohmann::json &value) {
  if ((name == "metrics" || name == "hc") && value.is_object()) {
    return masked(value, nullptr);
  }
  if (name == "events" && value.is_array()) {
    nlohmann::json events = nlohmann::json::array();
    for (const auto &event : value) {
      if (!event.is_object()) {
        events.push_back(event);
        continue;
      }
      std::set<std::string> fields = MASKED_EVENT_FIELDS;
      const bool view = event.value("key", "") == "[CLY]_view";
      if (view) {
        fields.insert("dur");
      }
      nlohmann::json maskedEvent = masked(event, &fields);
      if (view && maskedEvent.contains("segmentation") && maskedEvent["segmentation"].is_object()) {
        maskedEvent["segmentation"] = masked(maskedEvent["segmentation"], &MASKED_VIEW_SEGMENTS);
      }
      events.push_back(maskedEvent);
    }
    return events;
  }
  if (name == "crash" && value.is_object()) {
    return masked(value, &MASKED_CRASH_FIELDS);
  }
  return value;
}

/**
 * Decodes the %XX escapes of a request parameter.
 *
 * @param encoded: the value as the SDK sent it
 * @return the decoded value
 */
std::string decoded(const std::string &encoded) {
  std::string value;
  for (size_t i = 0; i < encoded.size(); i++) {
    if (encoded[i] == '%' && i + 2 < encoded.size()) {
      value += static_cast<char>(std::strtol(encoded.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      value += encoded[i];
    }
  }
  return value;
}

/**
 * HTTP client of the scenarios: keeps every request as the record writes it and answers it with success.
 *
 * @param use_post: whether the SDK sends the request as POST
 * @param url: path of the request
 * @param data: the URL encoded parameters
 * @return a successful response
 */
HTTPResponse recordRequest(bool use_post, const std::string &url, const std::string &data) {
  (void)use_post;
  nlohmann::json params = nlohmann::json::object();
  std::stringstream pairs(data);
  std::string pair;
  while (std::getline(pairs, pair, '&')) {
    const size_t assignment = pair.find('=');
    const std::string name = pair.substr(0, assignment);
    const std::string value = assignment == std::string::npos ? "" : decoded(pair.substr(assignment + 1));
    if (MASKED_PARAMS.count(name) > 0) {
      params[name] = MASK;
    } else if (JSON_PARAMS.count(name) > 0) {
      nlohmann::json parsed = nlohmann::json::parse(value, nullptr, false);
      params[name] = parsed.is_discarded() ? nlohmann::json(value) : maskedJson(name, parsed);
    } else {
      params[name] = value;
    }
  }
  nlohmann::ordered_json request;
  request["path"] = url;
  request["params"] = params;
  std::lock_guard<std::mutex> lock(sent_mutex);
  sent.push_back(request);
  return HTTPResponse{true, nlohmann::json({{"result", "Success"}}), 200};
}

/**
 * The number of requests sent so far, or of those that carry the given parameter.
 *
 * @param param: parameter to look for, or empty to count every request
 * @return the count
 */
size_t sentCount(const std::string &param = "") {
  std::lock_guard<std::mutex> lock(sent_mutex);
  size_t count = 0;
  for (const auto &request : sent) {
    if (param.empty() || request["params"].contains(param)) {
      count++;
    }
  }
  return count;
}

/**
 * Moves the recorded events into the request queue and waits until the queue is sent, so that what the calls before
 * queued stays apart from what follows.
 *
 * @param countly: the SDK
 */
void settle(Countly &countly) {
  countly.flushEvents();
  countly.processRQDebug();
  // processRQDebug returns at once while the update loop is sending the queue
  for (int waited = 0; waited < 10000 && countly.checkRQSize() > 0; waited += 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

/**
 * Describes where a record and the requests the SDK sent part: the first line that differs.
 *
 * @param record: content of the record
 * @param current: the record of the requests the SDK sent
 * @return the line number with both versions of the line, or an empty string when they are the same
 */
std::string firstDifference(const std::string &record, const std::string &current) {
  std::stringstream recorded(record);
  std::stringstream sentNow(current);
  std::string recordedLine;
  std::string sentLine;
  for (int line = 1;; line++) {
    const bool moreRecorded = static_cast<bool>(std::getline(recorded, recordedLine));
    const bool moreSent = static_cast<bool>(std::getline(sentNow, sentLine));
    if (!moreRecorded && !moreSent) {
      return "";
    }
    if (moreRecorded != moreSent || recordedLine != sentLine) {
      return "line " + std::to_string(line) + ": the record has [" + (moreRecorded ? recordedLine : "(end)") + "], the SDK sent [" + (moreSent ? sentLine : "(end)") + "]";
    }
  }
}

/**
 * Runs a scenario on a new SDK instance and compares the requests it sent with the record of the scenario, or writes
 * the record when COUNTLY_UPDATE_REQUESTS is set.
 *
 * @param name: scenario name, the record file name without .json
 * @param requiresConsent: whether the scenario starts the SDK with consent required
 * @param scenario: the calls, given the SDK
 */
void checkRequests(const std::string &name, bool requiresConsent, const std::function<void(Countly &)> &scenario) {
  test_utils::clearSDK();
  {
    std::lock_guard<std::mutex> lock(sent_mutex);
    sent.clear();
  }
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(recordRequest);
  countly.setDeviceID("parity-device-001");
  countly.SetPath(TEST_DATABASE_NAME);
  countly.enableManualSessionControl();
  countly.setRequiresConsent(requiresConsent);
  countly.setMetrics("parity-os", "1.0", "parity-device", "1x1", "parity-carrier", "1.0");
  // the settings are fetched on a thread of their own, whose requests would come between the queued ones at random
  countly.disableSDKBehaviorSettingsUpdates();
  countly.start("parity-app-key", "https://parity.example", 443, false);
  // the update loop sends the health check when it starts
  for (int waited = 0; waited < 5000 && sentCount("hc") == 0; waited += 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  scenario(countly);
  settle(countly);

  std::string current;
  {
    std::lock_guard<std::mutex> lock(sent_mutex);
    current = nlohmann::ordered_json(sent).dump(2) + "\n";
  }
  test_utils::clearSDK();

  const std::string file = RECORDS + name + ".json";
  if (std::getenv("COUNTLY_UPDATE_REQUESTS") != nullptr) {
    std::ofstream out(file, std::ios::binary);
    out << current;
    REQUIRE(out.good());
    return;
  }
  std::ifstream in(file, std::ios::binary);
  std::stringstream committed;
  committed << in.rdbuf();
  std::string record = committed.str();
  for (size_t at = record.find("\r\n"); at != std::string::npos; at = record.find("\r\n", at)) {
    record.erase(at, 1);
  }
  const std::string difference = firstDifference(record, current);
  INFO("the requests of the " << name << " scenario differ from api/requests/" << name << ".json. If that is deliberate, run the tests with COUNTLY_UPDATE_REQUESTS=1, check the difference and add a line to CHANGELOG.md.");
  CHECK_MESSAGE(difference.empty(), difference);
}
} // namespace

TEST_CASE("request scenario: parity") {
  checkRequests("parity", false, [](Countly &countly) {
    countly.beginSession();
    Event event("purchase", 2, 9.99, 5);
    event.addSegmentation("nar", "uto");
    event.addSegmentation("sasuke", MAX_INT);
    event.addSegmentation("sakura", false);
    event.addSegmentation("itachi", std::numeric_limits<double>::max());
    event.addSegmentation("kakaski", std::numeric_limits<double>::denorm_min());
    event.addSegmentation("hashirama", -MAX_INT);
    event.addSegmentation("tobi", nlohmann::json({{"obito", "uchiha"}, {"zetsu", "white"}}));
    event.addSegmentation("stringList", std::vector<std::string>{"value1", "value2", "value3"});
    event.addSegmentation("intList", std::vector<int>{1, 2, 3});
    event.addSegmentation("doubleList", std::vector<double>{1.1, 2.2, 3.3});
    event.addSegmentation("boolList", std::vector<bool>{true, false, true});
    event.addSegmentation("mixedList", nlohmann::json::array({"value1", 2, 3.3, true}));
    event.addSegmentation("mapList", nlohmann::json::array({nlohmann::json({{"key1", "value1"}, {"key2", 2}}), nlohmann::json({{"key1", "value2"}, {"key2", 3}}), nlohmann::json({{"key1", "value3"}, {"key2", 4}})}));
    countly.addEvent(event);
    countly.setUserDetails({{"name", "John Doe"}, {"username", "john_d"}, {"email", "john@example.com"}, {"organization", "Countly"}, {"phone", "1234567890"}, {"picture", "https://example.com/picture.jpg"}, {"gender", "M"}, {"byear", "1980"}});
    countly.setCustomUserDetails({{"tier", "gold"}, {"age", "30"}});
    // a session update reports elapsed seconds, so at least one has to pass for it to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    countly.updateSession();
    countly.endSession();
  });
}

TEST_CASE("request scenario: views") {
  checkRequests("views", false, [](Countly &countly) {
    countly.beginSession();
    countly.views().openView("Home", {{"source", "scenario"}});
    countly.views().closeViewWithName("Home");
    countly.views().openView("Settings");
    countly.endSession();
  });
}

TEST_CASE("request scenario: crash") {
  checkRequests("crash", false, [](Countly &countly) {
    countly.crash().addBreadcrumb("breadcrumb one");
    countly.crash().recordException("parity crash", "parity crash\n  at the scenario", false, {{"_os", "parity-os"}, {"_app_version", "1.0"}}, {{"tier", "gold"}});
  });
}

TEST_CASE("request scenario: consent") {
  checkRequests("consent", true, [](Countly &countly) {
    countly.giveConsent({"sessions", "events"});
    countly.beginSession();
    countly.RecordEvent("consented", 1);
    settle(countly);
    countly.removeConsent({"events"});
    countly.RecordEvent("not_consented", 1);
    countly.endSession();
  });
}

TEST_CASE("request scenario: device-id") {
  checkRequests("device-id", false, [](Countly &countly) {
    countly.beginSession();
    countly.setDeviceID("parity-device-002", true);
    countly.RecordEvent("after_merge", 1);
    settle(countly);
    countly.setDeviceID("parity-device-003", false);
    countly.RecordEvent("after_new_id", 1);
    countly.endSession();
  });
}
