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
class ConsentModule;

class CrashModule {

public:
  ~CrashModule();
  CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex);
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
   * @param crashMetrics: contains device information e.g app version, OS.
   * In crash metrics '_os', '_app_version' and '_error' are mandatory.
   * @param fatal: For automatically captured errors, you should set to 'true', whereas on logged errors it should be 'false'.
   */
  void recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation = {});

  /**
   * Removes every breadcrumb recorded so far, so that the next crash report starts with an empty trail.
   */
  void clearBreadcrumbs();

private:
  friend class Countly;
  class CrashModuleImpl;
  std::unique_ptr<CrashModuleImpl> impl;
  void setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider); // try injecting

  /**
   * Sets the request queue that crash reports go to. Set by start(); until then crash calls are ignored.
   */
  void setRequestModule(std::shared_ptr<RequestModule> requestModule);

  /**
   * Sets the SDK instance the module asks for the device metrics and the current view.
   */
  void setDelegates(cly::CountlyDelegates *cly);

  /**
   * Sets the consent state the crash calls check for the "crashes" consent.
   */
  void setConsentModule(std::shared_ptr<ConsentModule> consent);

  /**
   * Forgets every breadcrumb, without counting it as a call of the app.
   */
  void forgetBreadcrumbs();

  /**
   * Turns on or off the copy of the crash-time metrics and the breadcrumbs into the memory block that the native crash
   * handler's dumps contain. On while the handler is installed.
   */
  void setNativeContextEnabled(bool enabled);

  /**
   * Queues a native crash dump as an unhandled crash: the dump goes Base64-encoded into "_error" with "_native_cpp",
   * and is never cut by the internal limits. The metrics and breadcrumbs saved in the dump at crash time win over the
   * current ones. The crash filter sees the report, with nativeCrash set. The dump is taken over and freed once it is
   * encoded. The caller checks the "crashes" consent.
   */
  void recordNativeCrash(std::string dump);
};
} // namespace cly
#endif
