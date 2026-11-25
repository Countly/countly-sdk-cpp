/**
 * Crash helper for tests/native_crash.cpp. It installs a crash handler of its own first, as another crash tool would,
 * then starts the SDK with native crash capture, adds two breadcrumbs and crashes.
 *
 * Usage: countly-native-crash-helper <dump directory> <segv|abort|throw|purecall|invalid-parameter> <marker file>
 * The helper's own handler writes the marker file, which proves that it still runs after the SDK's handler.
 */
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "countly.hpp"

namespace {
const char *marker_path = nullptr;

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;

void writeMarker() {
  FILE *marker = std::fopen(marker_path, "wb");
  if (marker != nullptr) {
    std::fputs("previous handler ran", marker);
    std::fclose(marker);
  }
}

LONG WINAPI markerFilter(EXCEPTION_POINTERS *info) {
  writeMarker();
  return previous_filter != nullptr ? previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

void markerAbortHandler(int) { writeMarker(); }

void installOwnHandlers() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef _MSC_VER
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
#endif
  previous_filter = SetUnhandledExceptionFilter(markerFilter);
  signal(SIGABRT, markerAbortHandler);
}
#else
void markerSignalHandler(int sig) {
  const int marker = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (marker >= 0) {
    const char text[] = "previous handler ran";
    (void)!write(marker, text, sizeof(text) - 1);
    close(marker);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

void installOwnHandlers() {
  signal(SIGSEGV, markerSignalHandler);
  signal(SIGABRT, markerSignalHandler);
}
#endif

cly::HTTPResponse refuseEverything(bool, const std::string &, const std::string &) { return cly::HTTPResponse{false, nlohmann::json::object()}; }

volatile int *null_pointer = nullptr;

/**
 * Calls its pure virtual method while it is being constructed, which ends in the C++ runtime's pure virtual call
 * handler.
 */
struct PureBase {
  PureBase() { callPure(); }
  virtual ~PureBase() = default;
  void callPure() { pure(); }
  virtual void pure() = 0;
};

struct PureDerived : PureBase {
  void pure() override {}
};

#ifdef _MSC_VER
const char *volatile null_format = nullptr;

void tolerantParameterHandler(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t) {}
#endif
} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <dump directory> <segv|abort|throw> <marker file>\n", argv[0]);
    return 2;
  }
  const std::string mode = argv[2];
  marker_path = argv[3];
  installOwnHandlers();
#ifdef _MSC_VER
  if (mode == "tolerated-parameter") {
    _set_invalid_parameter_handler(tolerantParameterHandler);
  }
#endif

  cly::Countly &countly = cly::Countly::getInstance();
  countly.setHTTPClient(refuseEverything);
  countly.disableHealthCheck();
  countly.disableSDKBehaviorSettingsUpdates();
  countly.setDeviceID("native-crash-helper");
  countly.setMetrics("", "", "", "", "", "7.7");
  countly.setCrashDumpDirectory(argv[1]);
  countly.enableCrashReporting();
  countly.start("native-crash-helper-app-key", "http://localhost.invalid", 0);
  countly.crash().addBreadcrumb("helper started");
  countly.crash().addBreadcrumb("about to crash");

  if (mode == "segv") {
    *null_pointer = 42;
  } else if (mode == "abort") {
    std::abort();
  } else if (mode == "throw") {
    throw std::runtime_error("uncaught in the crash helper");
  } else if (mode == "purecall") {
    PureDerived derived;
    (void)derived;
#ifdef _MSC_VER
  } else if (mode == "invalid-parameter") {
    std::printf(null_format);
  } else if (mode == "tolerated-parameter") {
    std::printf(null_format);
    countly.stop();
    return 7;
#endif
  }
  return 1;
}
