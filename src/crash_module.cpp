#include "countly/crash_module.hpp"
#include "countly/request_module.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <iterator>

namespace cly {
class CrashModule::CrashModuleImpl {
public:
  std::deque<std::string> _breadCrumbs;
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

void CrashModule::addBreadcrumb(const std::string &value) {
  impl->_logger->log(LogLevel::INFO, "[CrashModule] addBreadcrumb : " + value);

  if (impl->_breadCrumbs.size() >= impl->_configuration->breadcrumbsThreshold) {
    impl->_breadCrumbs.pop_front();
  }

  impl->_breadCrumbs.push_back(value);
}

void CrashModule::recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(LogLevel::INFO, cly::utils::format_string("[CrashModule] recordException: title = %s, stackTrace = %s", title.c_str(), stackTrace.c_str()));

  if (title.empty()) {
    impl->_logger->log(LogLevel::WARNING, "[CrashModule] recordException : The parameter 'title' can't be empty");
  }

  if (stackTrace.empty()) {
    impl->_logger->log(LogLevel::WARNING, "[CrashModule] recordException : The parameter 'stackTrace' can't be empty");
  }

  std::ostringstream outstream;
  std::copy(impl->_breadCrumbs.begin(), impl->_breadCrumbs.end(), std::ostream_iterator<std::string>(outstream, "\n"));

  nlohmann::json crash(crashMetrics);
  nlohmann::json segments(segmentation);

  crash["_name"] = title;
  crash["_error"] = stackTrace;
  crash["_logs"] = outstream.str();
  crash["_custom"] = segments;
  crash["_nonfatal"] = !fatal;

  std::map<std::string, std::string> data = {{"crash", crash.dump()}};
  impl->_requestModule->addRequestToQueue(data);
}

} // namespace cly