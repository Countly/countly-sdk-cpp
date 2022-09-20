#include "countly/crash_module.hpp"
#include "countly/request_module.hpp"

namespace cly {
class CrashModule::CrashModuleImpl {
public:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestModule> _requestModule;
  CrashModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule) : _configuration(config), _logger(logger), _requestModule(requestModule) {}

  ~CrashModuleImpl() { _logger.reset(); }
};

CrashModule::~CrashModule() { impl.reset(); }

CrashModule::CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule) {
  impl.reset(new CrashModuleImpl(config, logger, requestModule));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[CrashModule] Initialized"));
}

/// <summary>
/// Adds string value to a list which is later sent over as logs whenever a cash is reported by system.
/// </summary>
/// <param name="value">a bread crumb for the crash report</param>
void addBreadcrumb(std::string value);

/// <summary>
/// Public method that sends crash details to the server. Set param "fatal" to false for Custom Logged errors
/// </summary>
/// <param name="error">a string that contain detailed description of the exception.</param>
/// <param name="stackTrace">a string that describes the contents of the call-stack.</param>
/// <param name="type">the type of the log message</param>
/// <param name="segments">custom key/values to be reported</param>
/// <param name="fatal">For automatically captured errors, you should set to <code>true</code>, whereas on logged errors it should be <code>false</code></param>
/// <returns></returns>
void CrashModule::recordException(std::string error, std::string stackTrace, bool fatal, std::map<std::string, std::string> crashMetrics, std::map<std::string, std::string> segmentation) {}

void addBreadcrumb(std::string value) {}

} // namespace cly