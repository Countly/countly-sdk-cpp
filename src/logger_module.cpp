#include "countly/logger_module.hpp"
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
namespace cly {
class LoggerModule::LoggerModuleImpl {
public:
  LoggerModuleImpl() {}
  std::mutex mutex;
  // Mirrors whether logger_function is set, so log() can bail out without
  // touching the mutex. Most integrators never register a callback, and the SDK
  // logs at DEBUG level on per-event paths.
  std::atomic<bool> has_logger{false};
  LoggerFunction logger_function;
};

LoggerModule::LoggerModule() { impl = std::make_unique<LoggerModuleImpl>(); }

LoggerModule::~LoggerModule() {}

void LoggerModule::setLogger(LoggerFunction logger) {
  std::lock_guard<std::mutex> lk(impl->mutex);
  impl->logger_function = logger;
  impl->has_logger.store(impl->logger_function != nullptr, std::memory_order_release);
}

const LoggerFunction LoggerModule::getLogger() {
  std::lock_guard<std::mutex> lk(impl->mutex);
  return impl->logger_function;
}

void LoggerModule::log(LogLevel level, const std::string &message) {
  // Fast path for the common case of no registered callback: one atomic load and
  // out, no lock. Without this, adding the mutex would have made every DEBUG log
  // site on the per-event paths more expensive than before it existed.
  if (!impl->has_logger.load(std::memory_order_acquire)) {
    return;
  }

  // Copy the callback under the lock and invoke it outside: the callback belongs
  // to the integrator and may call back into the SDK, which would re-enter this
  // function and deadlock on a non-recursive mutex.
  LoggerFunction callback;
  {
    std::lock_guard<std::mutex> lk(impl->mutex);
    callback = impl->logger_function;
  }

  if (callback != nullptr) {
    callback(level, message);
  }
}
} // namespace cly
