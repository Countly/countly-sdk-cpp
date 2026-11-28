#include "native_crash_breakpad.hpp"
#include "native_crash.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#include <windows.h>

#include "client/windows/handler/exception_handler.h"
#else
#include "client/linux/handler/exception_handler.h"
#endif

namespace cly {
namespace native_crash {
namespace breakpad {
namespace {

std::mutex handler_mutex;
// Created at the first install and never destroyed: destroying it would put back the handlers of that moment, over the
// handlers that other code installed later, and on Linux would free the signal stack of the thread that created it
google_breakpad::ExceptionHandler *handler = nullptr;
// The directory the handler writes into, the one of its first install
std::string handler_directory;
// Read in the crash path, where no lock may be taken
std::atomic<google_breakpad::ExceptionHandler *> active_handler(nullptr);
// Whether a crash writes a dump; when off, the crash goes on to the handler that was installed before
std::atomic<bool> enabled(false);
// Set when a dump of this crash exists, so that an abort() that follows the first crash report writes no second dump
std::atomic<bool> dump_written(false);

#ifdef _WIN32
typedef void (*SignalHandler)(int);
SignalHandler previous_abort_handler = SIG_DFL;

/**
 * Called by Breakpad before it writes a dump. Returns false while the handler is off, so that Breakpad writes nothing and
 * hands the crash to the handler that was installed before it.
 */
bool shouldWriteDump(void *, EXCEPTION_POINTERS *, MDRawAssertionInfo *) { return enabled.load(); }

/**
 * Called by Breakpad after it wrote a dump. Returns false, so that Breakpad hands the crash to the handler that was
 * installed before it, and the process ends the way it would without the SDK.
 */
bool onDumpWritten(const wchar_t *, const wchar_t *, void *, EXCEPTION_POINTERS *, MDRawAssertionInfo *, bool succeeded) {
  if (succeeded) {
    dump_written.store(true);
  }
  return false;
}

/**
 * Writes a dump of the calling thread with an exception record of the given code, unless this crash already has a dump.
 * The record lets the server's minidump_stackwalk mark the crashed thread.
 */
void writeDumpForCode(DWORD code) {
  google_breakpad::ExceptionHandler *current = active_handler.load();
  if (current == nullptr || !enabled.load() || dump_written.load()) {
    return;
  }
  EXCEPTION_RECORD record = {};
  CONTEXT context = {};
  RtlCaptureContext(&context);
  record.ExceptionCode = code;
#if defined(_M_X64) || defined(__x86_64__)
  record.ExceptionAddress = reinterpret_cast<PVOID>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
  record.ExceptionAddress = reinterpret_cast<PVOID>(context.Eip);
#elif defined(_M_ARM64) || defined(__aarch64__)
  record.ExceptionAddress = reinterpret_cast<PVOID>(context.Pc);
#endif
  EXCEPTION_POINTERS pointers = {&record, &context};
  current->WriteMinidumpForException(&pointers);
}

/**
 * Writes a dump for abort(), which ends the process without an exception that Breakpad would see, then calls the
 * SIGABRT handler that was installed before. A pure virtual call also arrives here, through abort().
 */
void onAbort(int signal_number) {
  // STATUS_FATAL_APP_EXIT, the code Windows reports for abort()
  writeDumpForCode(0x40000015);
  const SignalHandler previous = previous_abort_handler;
  if (previous != SIG_DFL && previous != SIG_IGN && previous != SIG_ERR && previous != nullptr) {
    previous(signal_number);
  }
}

#ifdef _MSC_VER
_invalid_parameter_handler previous_parameter_handler = nullptr;

/**
 * Handles an invalid parameter given to a C runtime function. An earlier handler of the app decides alone, because it
 * may let the function fail and the app go on. Without one, the SDK writes a dump and runs the C runtime's default,
 * which ends the process, as it would without the SDK.
 */
void onInvalidParameter(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line, uintptr_t reserved) {
  const _invalid_parameter_handler previous = previous_parameter_handler;
  if (previous != nullptr) {
    previous(expression, function, file, line, reserved);
    return;
  }
  // STATUS_INVALID_CRUNTIME_PARAMETER
  writeDumpForCode(0xC0000417);
  _set_invalid_parameter_handler(nullptr);
#ifdef _DEBUG
  _invalid_parameter(expression, function, file, line, reserved);
#else
  _invalid_parameter_noinfo();
#endif
}
#endif
#else
/**
 * Called by Breakpad before it writes a dump. Returns false while the handler is off, so that Breakpad writes nothing,
 * puts back the signal handlers that were installed before it and raises the signal again.
 */
bool shouldWriteDump(void *) { return enabled.load(); }

/**
 * Called by Breakpad after it wrote a dump. Returns false, so that Breakpad puts back the signal handlers that were
 * installed before it and raises the signal again: the process ends the way it would without the SDK.
 */
bool onDumpWritten(const google_breakpad::MinidumpDescriptor &, void *, bool succeeded) {
  if (succeeded) {
    dump_written.store(true);
  }
  return false;
}
#endif

} // namespace

bool install(const std::string &absoluteDirectory, void *appMemory, size_t appMemorySize, std::string &error) {
  std::lock_guard<std::mutex> lock(handler_mutex);
  if (enabled.load()) {
    error = "the crash handler is already on";
    return false;
  }
  if (handler != nullptr && absoluteDirectory != handler_directory) {
    error = "the crash handler of this process writes its dumps into [" + handler_directory + "], and it can't move to another directory";
    return false;
  }
  dump_written.store(false);
  if (handler == nullptr) {
#ifdef _WIN32
    // Breakpad's own invalid parameter and pure virtual call handlers end the process with exit code 0, so the SDK
    // installs only its exception filter and handles those two cases itself
    handler = new google_breakpad::ExceptionHandler(widen(absoluteDirectory), shouldWriteDump, onDumpWritten, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION);
#else
    handler = new google_breakpad::ExceptionHandler(google_breakpad::MinidumpDescriptor(absoluteDirectory), shouldWriteDump, onDumpWritten, nullptr, true, -1);
#endif
    handler->RegisterAppMemory(appMemory, appMemorySize);
    handler_directory = absoluteDirectory;
    active_handler.store(handler);
#ifdef _WIN32
    previous_abort_handler = signal(SIGABRT, onAbort);
#endif
#ifdef _MSC_VER
    previous_parameter_handler = _set_invalid_parameter_handler(onInvalidParameter);
#endif
  }
  enabled.store(true);
  return true;
}

void uninstall() {
  std::lock_guard<std::mutex> lock(handler_mutex);
  enabled.store(false);
}

bool isInstalled() { return enabled.load(); }

} // namespace breakpad
} // namespace native_crash
} // namespace cly
