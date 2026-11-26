/**
 * Crash helper for tests/native_crash.cpp. It installs a crash handler of its own first, as another crash tool would,
 * then starts the SDK with native crash capture, adds two breadcrumbs and crashes.
 *
 * Usage: countly-native-crash-helper <dump directory>
 *        <segv|abort|throw|purecall|invalid-parameter|tolerated-parameter|segv-after-stop|off-on-other-thread> <marker file>
 * The helper's own handler writes the marker file, which proves that it still runs after the SDK's handler.
 * "off-on-other-thread" (POSIX) does not crash: it calls stop() on a second thread and exits with 0 while the memory of
 * the main thread's signal stack is still its own, or with 3 when that memory was handed out again.
 */
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#include <fstream>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _MSC_VER
extern "C" int __cdecl _purecall(void);
#else
#include <cxxabi.h>
#endif

#include "countly.hpp"

namespace {
const char *marker_path = nullptr;

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;

void writeMarker() {
  std::ofstream marker(marker_path, std::ios::binary | std::ios::trunc);
  marker << "previous handler ran";
}

LONG WINAPI markerFilter(EXCEPTION_POINTERS *info) {
  writeMarker();
  return previous_filter != nullptr ? previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

void markerAbortHandler(int) { writeMarker(); }

#ifdef _MSC_VER
void tolerantParameterHandler(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t) {}
#endif

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
  // a signal handler may only call async-signal-safe functions, so the marker is written with open and write
  const int marker = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR); // Flawfinder: ignore
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

/**
 * Whether the memory of this thread's alternate signal stack is still its own: none of 64 new blocks of the stack's
 * size lies on it. The allocator hands out the memory of a freed stack again.
 */
bool signalStackIsIntact() {
  stack_t current = {};
  if (sigaltstack(nullptr, &current) != 0 || (current.ss_flags & SS_DISABLE) != 0 || current.ss_sp == nullptr) {
    return true;
  }
  const char *stackStart = static_cast<const char *>(current.ss_sp);
  const char *stackEnd = stackStart + current.ss_size;
  std::vector<char *> blocks;
  bool intact = true;
  for (int index = 0; index < 64; index++) {
    char *block = static_cast<char *>(std::calloc(1, current.ss_size));
    blocks.push_back(block);
    if (block != nullptr && block < stackEnd && block + current.ss_size > stackStart) {
      intact = false;
    }
  }
  for (char *block : blocks) {
    std::free(block);
  }
  return intact;
}
#endif

/**
 * Runs the C++ runtime's handler for a call of a pure virtual function, the code such a call ends in.
 */
void callPureVirtualHandler() {
#ifdef _MSC_VER
  _purecall();
#else
  __cxxabiv1::__cxa_pure_virtual();
#endif
}

cly::HTTPResponse refuseEverything(bool, const std::string &, const std::string &) { return cly::HTTPResponse{false, nlohmann::json::object()}; }

volatile int *null_pointer = nullptr;
} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <dump directory> <crash mode> <marker file>\n", argv[0]);
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
  } else if (mode == "segv-after-stop") {
    countly.stop();
    *null_pointer = 42;
#ifndef _WIN32
  } else if (mode == "off-on-other-thread") {
    std::thread([&countly] { countly.stop(); }).join();
    return signalStackIsIntact() ? 0 : 3;
#endif
  } else if (mode == "abort") {
    std::abort();
  } else if (mode == "throw") {
    throw std::runtime_error("uncaught in the crash helper");
  } else if (mode == "purecall") {
    callPureVirtualHandler();
#ifdef _MSC_VER
  } else if (mode == "invalid-parameter") {
    _close(-1);
  } else if (mode == "tolerated-parameter") {
    _close(-1);
    countly.stop();
    return 7;
#endif
  }
  return 1;
}
