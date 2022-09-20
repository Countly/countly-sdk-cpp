#ifndef CRASH_MODULE_HPP_
#define CRASH_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/request_module.hpp"

namespace cly {
class CrashModule {

public:
  ~CrashModule();
  CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule);

  void recordException(std::string error, std::string stackTrace, bool fatal, std::map<std::string, std::string> crashMetrics, std::map<std::string, std::string> segmentation);

private:
  class CrashModuleImpl;
  std::unique_ptr<CrashModuleImpl> impl;
};
} // namespace cly
#endif
