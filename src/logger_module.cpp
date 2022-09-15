#include "countly/logger_module.hpp"
#include <iostream>
#include <memory>
namespace cly {
class LoggerModule::LoggerModuleImpl {
public:
  LoggerModuleImpl() {}
  LoggerFunction logger_function;
};

LoggerModule::LoggerModule() { impl = std::make_unique<LoggerModuleImpl>(); }

LoggerModule::~LoggerModule() {}

void LoggerModule::setLogger(LoggerFunction logger) { impl->logger_function = logger; }

const LoggerFunction LoggerModule::getLogger() { return impl->logger_function; }

void LoggerModule::log(LogLevel level, const std::string &message) {
  if (impl->logger_function != nullptr) {
    impl->logger_function(level, message);
  }
}
} // namespace cly
