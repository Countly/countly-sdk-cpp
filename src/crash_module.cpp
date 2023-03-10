#include "countly/crash_module.hpp"
#include "countly/request_module.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <iterator>

// define namespace
namespace cly {
// define CrashModuleImpl class
class CrashModule::CrashModuleImpl {
public:
  std::deque<std::string> _breadCrumbs; // breadcrumbs deque
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestModule> _requestModule;
  std::shared_ptr<std::mutex> _mutex;
  CrashModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) : _configuration(config), _logger(logger), _requestModule(requestModule), _mutex(mutex) {}

  // destructor to reset logger
  ~CrashModuleImpl() { _logger.reset(); }
};

// destructor to reset implementation
CrashModule::~CrashModule() { impl.reset(); }

CrashModule::CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) {
  impl.reset(new CrashModuleImpl(config, logger, requestModule, mutex));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[CrashModule] Initialized"));
}

// function to add breadcrumb
void CrashModule::addBreadcrumb(const std::string &value) {
  impl->_logger->log(LogLevel::INFO, "[CrashModule] addBreadcrumb : " + value);

  impl->_mutex->lock();
  // if breadcrumb threshold is reached, remove oldest breadcrumb
  if (impl->_breadCrumbs.size() >= impl->_configuration->breadcrumbsThreshold) {
    impl->_breadCrumbs.pop_front();
  }
  // add new breadcrumb
  impl->_breadCrumbs.push_back(value);
  impl->_mutex->unlock();
}

// function to record exception
void CrashModule::recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(LogLevel::INFO, cly::utils::format_string("[CrashModule] recordException: title = %s, stackTrace = %s", title.c_str(), stackTrace.c_str()));

  if (title.empty()) {
    impl->_logger->log(LogLevel::WARNING, "[CrashModule] recordException : The parameter 'title' can't be empty");
  }

  if (stackTrace.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[CrashModule] recordException : The parameter 'stackTrace' can't be empty");
  }

  // check if the crash metric '_os' exists and is not empty
  auto it = crashMetrics.find("_os");
  if (it == crashMetrics.end() || it->second.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[CrashModule] recordException : The crash metric '_os' can't be empty");
  }

  // check if the crash metric '_app_version' exists and is not empty
  it = crashMetrics.find("_app_version");
  if (it == crashMetrics.end() || it->second.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[CrashModule] recordException : The crash metric '_app_version' can't be empty");
  }

  // lock mutex to avoid concurrent access
  impl->_mutex->lock();
  // convert breadcrumbs vector to a string and add to json object
  std::ostringstream outstream;
  std::copy(impl->_breadCrumbs.begin(), impl->_breadCrumbs.end(), std::ostream_iterator<std::string>(outstream, "\n"));

  // create json objects for crash metrics and segmentation
  nlohmann::json crash(crashMetrics);
  nlohmann::json segments(segmentation);

  // add relevant fields to the crash json object
  crash["_name"] = title;
  crash["_error"] = stackTrace;
  crash["_logs"] = outstream.str();
  crash["_custom"] = segments;
  crash["_nonfatal"] = !fatal;

  // create a map with the crash json object as value and "crash" as key, and add the map to the request queue
  std::map<std::string, std::string> data = {{"crash", crash.dump()}};
  impl->_requestModule->addRequestToQueue(data);
  // unlock mutex
  impl->_mutex->unlock();
}

} // namespace cly