#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "countly.hpp"
#include "doctest.h"
#include "native_crash.hpp"
#include "nlohmann/json.hpp"
#include "test_utils.hpp"

using namespace cly;
using namespace test_utils;
using json = nlohmann::json;

namespace {
const char *const DUMP_DIRECTORY = "native-crash-dumps";

/**
 * Creates the dump directory when it is missing and deletes every file the tests put in it.
 */
void resetDumpDirectory() {
#ifdef _WIN32
  _mkdir(DUMP_DIRECTORY);
#else
  mkdir(DUMP_DIRECTORY, 0755);
#endif
  for (const std::string &dump : native_crash::listDumpFiles(DUMP_DIRECTORY)) {
    native_crash::removeFile(dump);
  }
  for (const char *name : {"notes.txt", "marker.txt"}) {
    native_crash::removeFile(std::string(DUMP_DIRECTORY) + "/" + name);
  }
}

std::string pathInDumpDirectory(const std::string &name) { return std::string(DUMP_DIRECTORY) + "/" + name; }

void writeFile(const std::string &path, const std::string &content) {
  FILE *file = std::fopen(path.c_str(), "wb");
  REQUIRE(file != nullptr);
  REQUIRE(std::fwrite(content.data(), 1, content.size(), file) == content.size());
  std::fclose(file);
}

bool fileExists(const std::string &path) { return native_crash::fileSize(path) >= 0; }

/**
 * Bytes that look like a minidump: the signature, then binary data with line breaks and zero bytes.
 */
std::string fakeDump(size_t size) {
  std::string dump("MDMP\x93\xa7\x00\x00", 8);
  for (size_t index = dump.size(); index < size; index++) {
    dump.push_back(static_cast<char>((index * 31) % 256));
  }
  return dump;
}

std::string base64Decode(const std::string &text) {
  static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string bytes;
  unsigned int buffer = 0;
  int bits = 0;
  bool valid = true;
  for (char character : text) {
    if (character == '=') {
      break;
    }
    const std::string::size_type value = alphabet.find(character);
    valid = valid && value != std::string::npos;
    buffer = (buffer << 6) | static_cast<unsigned int>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  REQUIRE(valid);
  return bytes;
}

Countly &prepareNativeCrashes(Countly &countly) {
  countly.setHTTPClient(fakeSendHTTP);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.enableManualSessionControl();
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(600000);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.setLogger(captureLog);
  countly.setMetrics("", "", "Test PC", "1920x1080", "", "2.0");
  countly.setCrashDumpDirectory(DUMP_DIRECTORY);
  return countly;
}

Countly &prepareDefaultInstance() {
  clearSDK();
  clearCapturedLogs();
  resetDumpDirectory();
  return prepareNativeCrashes(Countly::getInstance());
}

void startNativeCrashes(Countly &countly) { countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT); }

std::vector<json> sentCrashes(Countly &countly) {
  countly.processRQDebug();
  std::vector<json> crashes;
  for (size_t index = 0; index < http_call_queue.size(); index++) {
    HTTPCall call = http_call_queue.at(index);
    if (call.data.count("crash")) {
      crashes.push_back(json::parse(call.data["crash"]));
    }
  }
  return crashes;
}

std::string dumpWithContext(const json &context) {
  const std::string block = native_crash::encodeContextBlock(context.dump());
  REQUIRE_FALSE(block.empty());
  return fakeDump(300) + block + fakeDump(200);
}
} // namespace

TEST_CASE("A dump in the crash dump directory is sent at start as an unhandled native crash, and the file is deleted") {
  Countly &countly = prepareDefaultInstance();
  const std::string dump = fakeDump(5000);
  writeFile(pathInDumpDirectory("first.dmp"), dump);
  countly.setCustomCrashSegmentation({{"build", "nightly"}});
  startNativeCrashes(countly);
  const std::string viewId = countly.views().openView("Home");
  (void)viewId;

  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  const json &crash = crashes[0];
  CHECK(crash["_native_cpp"] == true);
  CHECK(crash["_nonfatal"] == false);
  CHECK(base64Decode(crash["_error"].get<std::string>()) == dump);
  CHECK(crash["_app_version"] == "2.0");
  CHECK(crash["_device"] == "Test PC");
  CHECK(crash["_os"] == countly.PlatformName());
  CHECK(crash["_custom"] == json({{"build", "nightly"}}));
  CHECK(crash["_logs"] == "");
  for (const char *moment : {"_cvid", "_view", "_ram_current", "_disk_current", "_bat", "_run", "_orientation", "_online", "_muted", "_background"}) {
    CHECK_FALSE(crash.contains(moment));
  }
  CHECK_FALSE(fileExists(pathInDumpDirectory("first.dmp")));
}

TEST_CASE("Every dump is sent, one crash report each") {
  Countly &countly = prepareDefaultInstance();
  writeFile(pathInDumpDirectory("a.dmp"), fakeDump(100));
  writeFile(pathInDumpDirectory("B.DMP"), fakeDump(200));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).size() == 2);
  CHECK(native_crash::listDumpFiles(DUMP_DIRECTORY).empty());
}

TEST_CASE("Native crash dumps are never cut by the internal limits, also not after the crash filter") {
  Countly &countly = prepareDefaultInstance();
  countly.setMaxStackTraceLineLength(10);
  countly.setMaxStackTraceLinesPerThread(1);
  countly.setCrashFilterCallback([](CrashData &crash) {
    crash.crashSegmentation["filtered"] = "yes";
    return false;
  });
  const std::string dump = fakeDump(20000);
  writeFile(pathInDumpDirectory("big.dmp"), dump);
  startNativeCrashes(countly);

  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(base64Decode(crashes[0]["_error"].get<std::string>()) == dump);
  CHECK(crashes[0]["_custom"]["filtered"] == "yes");
  CHECK(crashes[0]["_ob"] == 8);
}

TEST_CASE("The crash filter sees the native crash and can drop it; a dropped dump is deleted") {
  Countly &countly = prepareDefaultInstance();
  bool sawNativeCrash = false;
  countly.setCrashFilterCallback([&sawNativeCrash](CrashData &crash) {
    sawNativeCrash = crash.fatal && crash.stackTrace.compare(0, 4, "TURN") == 0;
    return true;
  });
  writeFile(pathInDumpDirectory("dropped.dmp"), fakeDump(100));
  startNativeCrashes(countly);
  CHECK(sawNativeCrash);
  CHECK(sentCrashes(countly).empty());
  CHECK_FALSE(fileExists(pathInDumpDirectory("dropped.dmp")));
}

TEST_CASE("Without the crashes consent, a dump is deleted without being sent") {
  Countly &countly = prepareDefaultInstance();
  countly.setRequiresConsent(true);
  countly.setConsentEnabled({consent::SESSIONS});
  writeFile(pathInDumpDirectory("no-consent.dmp"), fakeDump(100));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).empty());
  CHECK_FALSE(fileExists(pathInDumpDirectory("no-consent.dmp")));
}

TEST_CASE("With the crashes consent given at start, a dump is sent") {
  Countly &countly = prepareDefaultInstance();
  countly.setRequiresConsent(true);
  countly.setConsentEnabled({consent::CRASHES});
  writeFile(pathInDumpDirectory("consent.dmp"), fakeDump(100));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).size() == 1);
}

TEST_CASE("Other files stay in the directory, and an empty dump is deleted with a warning") {
  Countly &countly = prepareDefaultInstance();
  writeFile(pathInDumpDirectory("notes.txt"), "keep me");
  writeFile(pathInDumpDirectory("empty.dmp"), "");
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).empty());
  CHECK(fileExists(pathInDumpDirectory("notes.txt")));
  CHECK_FALSE(fileExists(pathInDumpDirectory("empty.dmp")));
  CHECK(capturedLogContains("is empty"));
}

TEST_CASE("A dump larger than 10 MB is deleted without being sent, with an error") {
  Countly &countly = prepareDefaultInstance();
  writeFile(pathInDumpDirectory("huge.dmp"), fakeDump(static_cast<size_t>(native_crash::MAX_DUMP_BYTES) + 1));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).empty());
  CHECK_FALSE(fileExists(pathInDumpDirectory("huge.dmp")));
  CHECK(capturedLogContains("larger than"));
}

TEST_CASE("A dump of exactly 10 MB is sent") {
  Countly &countly = prepareDefaultInstance();
  writeFile(pathInDumpDirectory("limit.dmp"), fakeDump(static_cast<size_t>(native_crash::MAX_DUMP_BYTES)));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).size() == 1);
}

TEST_CASE("While the server's crash tracking setting is off, dumps stay for a later start") {
  Countly &countly = prepareDefaultInstance();
  countly.setSDKBehaviorSettings("{\"crt\":false}");
  writeFile(pathInDumpDirectory("later.dmp"), fakeDump(100));
  startNativeCrashes(countly);
  CHECK(sentCrashes(countly).empty());
  CHECK(fileExists(pathInDumpDirectory("later.dmp")));
}

TEST_CASE("Only the default instance handles native crashes") {
  clearSDK();
  clearCapturedLogs();
  resetDumpDirectory();
  writeFile(pathInDumpDirectory("default-only.dmp"), fakeDump(100));
  Countly &named = Countly::createInstance("second");
  prepareNativeCrashes(named);
  named.enableCrashReporting();
  named.setDatabasePath("native-crash-second.db");
  named.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT);
  CHECK(sentCrashes(named).empty());
  CHECK(fileExists(pathInDumpDirectory("default-only.dmp")));
  CHECK(capturedLogContains("only the default instance"));
  CHECK_FALSE(native_crash::isHandlerInstalled());
  Countly::destroyInstance("second");
  std::remove("native-crash-second.db");
}

TEST_CASE("The app version and breadcrumbs saved in the dump at crash time win over the current values") {
  Countly &countly = prepareDefaultInstance();
  writeFile(pathInDumpDirectory("context.dmp"), dumpWithContext({{"m", {{"_app_version", "1.0"}, {"_os", "TestOS"}}}, {"l", {"opened settings", "pressed save"}}}));
  startNativeCrashes(countly);
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_app_version"] == "1.0");
  CHECK(crashes[0]["_os"] == "TestOS");
  CHECK(crashes[0]["_device"] == "Test PC");
  CHECK(crashes[0]["_logs"] == "opened settings\npressed save\n");
}

TEST_CASE("A damaged context block in the dump is ignored, and the current values are used") {
  Countly &countly = prepareDefaultInstance();
  std::string dump = dumpWithContext({{"m", {{"_app_version", "1.0"}}}, {"l", {"opened settings"}}});
  const std::string::size_type payload = dump.find("opened settings");
  REQUIRE(payload != std::string::npos);
  dump[payload] = 'O';
  writeFile(pathInDumpDirectory("damaged.dmp"), dump);
  startNativeCrashes(countly);
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_app_version"] == "2.0");
  CHECK(crashes[0]["_logs"] == "");
}

TEST_CASE("A context block holds a payload up to its size and refuses a larger one") {
  CHECK_FALSE(native_crash::encodeContextBlock(std::string(native_crash::maxContextPayload(), 'a')).empty());
  CHECK(native_crash::encodeContextBlock(std::string(native_crash::maxContextPayload() + 1, 'a')).empty());
  std::string payload;
  CHECK(native_crash::findContextPayload(fakeDump(50) + native_crash::encodeContextBlock("{\"m\":{}}"), payload));
  CHECK(payload == "{\"m\":{}}");
  CHECK_FALSE(native_crash::findContextPayload(fakeDump(500), payload));
}

TEST_CASE("Base64 encoding follows the standard alphabet and padding") {
  CHECK(native_crash::base64Encode("") == "");
  CHECK(native_crash::base64Encode("f") == "Zg==");
  CHECK(native_crash::base64Encode("fo") == "Zm8=");
  CHECK(native_crash::base64Encode("foo") == "Zm9v");
  CHECK(native_crash::base64Encode(std::string("\xfb\xff\x00", 3)) == "+/8A");
}

#ifndef COUNTLY_USE_BREAKPAD
TEST_CASE("enableCrashReporting in a build without a crash handler logs a warning, and dumps are still sent") {
  Countly &countly = prepareDefaultInstance();
  countly.enableCrashReporting();
  writeFile(pathInDumpDirectory("still-sent.dmp"), fakeDump(100));
  startNativeCrashes(countly);
  CHECK(capturedLogContains("COUNTLY_USE_BREAKPAD"));
  CHECK_FALSE(native_crash::isHandlerInstalled());
  CHECK(sentCrashes(countly).size() == 1);
}
#else
TEST_CASE("The crash handler follows enableCrashReporting, the crashes consent and the server's settings") {
  SUBCASE("not installed without enableCrashReporting") {
    Countly &countly = prepareDefaultInstance();
    startNativeCrashes(countly);
    CHECK_FALSE(native_crash::isHandlerInstalled());
  }
  SUBCASE("installed at start, removed at stop") {
    Countly &countly = prepareDefaultInstance();
    countly.enableCrashReporting();
    startNativeCrashes(countly);
    CHECK(native_crash::isHandlerInstalled());
    countly.stop();
    CHECK_FALSE(native_crash::isHandlerInstalled());
  }
  SUBCASE("removed with the crashes consent and installed again when it is given") {
    Countly &countly = prepareDefaultInstance();
    countly.setRequiresConsent(true);
    countly.setConsentEnabled({consent::CRASHES});
    countly.enableCrashReporting();
    startNativeCrashes(countly);
    CHECK(native_crash::isHandlerInstalled());
    countly.removeConsent({consent::CRASHES});
    CHECK_FALSE(native_crash::isHandlerInstalled());
    countly.giveConsent({consent::CRASHES});
    CHECK(native_crash::isHandlerInstalled());
  }
  SUBCASE("the server's acr turns it off") {
    Countly &countly = prepareDefaultInstance();
    countly.setSDKBehaviorSettings("{\"acr\":false}");
    countly.enableCrashReporting();
    startNativeCrashes(countly);
    CHECK_FALSE(native_crash::isHandlerInstalled());
  }
  SUBCASE("the server's acr turns it on") {
    Countly &countly = prepareDefaultInstance();
    countly.setSDKBehaviorSettings("{\"acr\":true}");
    startNativeCrashes(countly);
    CHECK(native_crash::isHandlerInstalled());
  }
  SUBCASE("the server's crt turns it off") {
    Countly &countly = prepareDefaultInstance();
    countly.setSDKBehaviorSettings("{\"crt\":false}");
    countly.enableCrashReporting();
    startNativeCrashes(countly);
    CHECK_FALSE(native_crash::isHandlerInstalled());
  }
  SUBCASE("a missing directory is an error") {
    clearSDK();
    clearCapturedLogs();
    Countly &countly = prepareNativeCrashes(Countly::getInstance());
    countly.setCrashDumpDirectory("native-crash-dumps-missing");
    countly.enableCrashReporting();
    startNativeCrashes(countly);
    CHECK_FALSE(native_crash::isHandlerInstalled());
    CHECK(capturedLogContains("native-crash-dumps-missing"));
  }
  clearSDK();
  CHECK_FALSE(native_crash::isHandlerInstalled());
}

namespace {
/**
 * Runs the crash helper, which starts the SDK with the crash handler, adds breadcrumbs and crashes in the given way.
 * Returns how the helper ended: the signal on POSIX systems, the exit code on Windows.
 */
long long runCrashHelper(const std::string &mode) {
  const std::string marker = pathInDumpDirectory("marker.txt");
#ifdef _WIN32
  const std::string command = std::string("\"") + COUNTLY_CRASH_HELPER_PATH + "\" " + DUMP_DIRECTORY + " " + mode + " " + marker;
  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  std::vector<char> line(command.begin(), command.end());
  line.push_back('\0');
  REQUIRE(CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
  WaitForSingleObject(process.hProcess, 60000);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return static_cast<long long>(code);
#else
  const pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    execl(COUNTLY_CRASH_HELPER_PATH, COUNTLY_CRASH_HELPER_PATH, DUMP_DIRECTORY, mode.c_str(), marker.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = 0;
  REQUIRE(waitpid(child, &status, 0) == child);
  REQUIRE(WIFSIGNALED(status));
  return WTERMSIG(status);
#endif
}

void checkCapturedCrash(const std::string &mode, long long expectedEnd, bool previousHandlerRuns = true) {
  Countly &countly = prepareDefaultInstance();
  CHECK(runCrashHelper(mode) == expectedEnd);
  CHECK(fileExists(pathInDumpDirectory("marker.txt")) == previousHandlerRuns);
  REQUIRE(native_crash::listDumpFiles(DUMP_DIRECTORY).size() == 1);

  startNativeCrashes(countly);
  std::vector<json> crashes = sentCrashes(countly);
  REQUIRE(crashes.size() == 1);
  CHECK(crashes[0]["_native_cpp"] == true);
  CHECK(crashes[0]["_nonfatal"] == false);
  CHECK(base64Decode(crashes[0]["_error"].get<std::string>()).compare(0, 4, "MDMP") == 0);
  CHECK(crashes[0]["_app_version"] == "7.7");
  CHECK(crashes[0]["_logs"] == "helper started\nabout to crash\n");
  CHECK(native_crash::listDumpFiles(DUMP_DIRECTORY).empty());
}
} // namespace

TEST_CASE("A crash is captured in a dump, the handler that was there before still runs, and the next start sends it") {
#ifdef _WIN32
  SUBCASE("access violation") { checkCapturedCrash("segv", static_cast<long long>(0xC0000005UL)); }
  SUBCASE("abort") { checkCapturedCrash("abort", 3); }
  SUBCASE("uncaught C++ exception") { checkCapturedCrash("throw", 3); }
  SUBCASE("pure virtual call") { checkCapturedCrash("purecall", 3); }
#ifdef _MSC_VER
  // the C runtime's default ends the process with a fast fail, which no handler of the app sees
  SUBCASE("invalid C runtime parameter") { checkCapturedCrash("invalid-parameter", static_cast<long long>(0xC0000409UL), false); }
  SUBCASE("an invalid parameter that the app's own handler tolerates is not a crash") {
    prepareDefaultInstance();
    CHECK(runCrashHelper("tolerated-parameter") == 7);
    CHECK(native_crash::listDumpFiles(DUMP_DIRECTORY).empty());
  }
#endif
#else
  SUBCASE("segmentation fault") { checkCapturedCrash("segv", SIGSEGV); }
  SUBCASE("abort") { checkCapturedCrash("abort", SIGABRT); }
  SUBCASE("uncaught C++ exception") { checkCapturedCrash("throw", SIGABRT); }
  SUBCASE("pure virtual call") { checkCapturedCrash("purecall", SIGABRT); }
#endif
}
#endif
