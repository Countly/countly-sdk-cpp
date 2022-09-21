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
  /**
   * Adds string value to a list which is later sent over as logs whenever a cash is reported by system.
   *
   * @param value: a bread crumb for the crash report
   */
  void addBreadcrumb(const std::string &value);

  /**
   * Public method that sends crash details to the server. Set param "fatal" to false for Custom Logged errors

   * @param title: a string that contains description of the exception.
   * @param stackTrace: a string that describes the contents of the call-stack.
   * @param segmentation: custom key/values to be reported
   * @param crashMetrics: contains crash information e.g app version, OS etc...
   * @param fatal: For automatically captured errors, you should set to 'true', whereas on logged errors it should be 'false'.
   */
  void recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation = {});

private:
  class CrashModuleImpl;
  std::unique_ptr<CrashModuleImpl> impl;
};
} // namespace cly
#endif
