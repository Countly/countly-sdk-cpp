#include "countly/crash_module.hpp"
#include "countly/internal_limits.hpp"
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
  std::weak_ptr<ConfigurationProvider> _configProvider;
  CrashModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) : _configuration(config), _logger(logger), _requestModule(requestModule), _mutex(mutex) {}

  // destructor to reset logger
  ~CrashModuleImpl() { _logger.reset(); }
};

// destructor to reset implementation
CrashModule::~CrashModule() { impl.reset(); }

CrashModule::CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) {
  impl.reset(new CrashModuleImpl(config, logger, requestModule, mutex));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [CrashModule] Initialized"));
}

// function to add breadcrumb
void CrashModule::addBreadcrumb(const std::string &value) {
  SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  if (std::shared_ptr<ConfigurationProvider> config = impl->_configProvider.lock()) {
    lim = config->getLimits();
  }

  std::string limited = cly::limits::truncateString(value, lim.maxValueSize);
  impl->_logger->log(LogLevel::INFO, "[Countly] [CrashModule] addBreadcrumb, value = [" + limited + "]");

  std::lock_guard<std::mutex> lk(*impl->_mutex);
  // if breadcrumb count limit is reached, remove oldest breadcrumb
  if (impl->_breadCrumbs.size() >= lim.maxBreadcrumbCount) {
    impl->_breadCrumbs.pop_front();
  }
  // add new breadcrumb
  impl->_breadCrumbs.push_back(limited);
}

// function to record exception
void CrashModule::recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(LogLevel::INFO, cly::utils::format_string("[Countly] [CrashModule] recordException, title = [%s], stackTrace = [%s]", title.c_str(), stackTrace.c_str()));

  SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  if (std::shared_ptr<ConfigurationProvider> config = impl->_configProvider.lock()) {
    if (config->isCrashReportingEnabled() == false) {
      impl->_logger->log(LogLevel::DEBUG, "[Countly] [CrashModule] recordException, Crash reporting is disabled. Not recording exception.");
      return;
    }
    lim = config->getLimits();
  } else {
    impl->_logger->log(LogLevel::WARNING, "[Countly] [CrashModule] recordException, ConfigurationProvider unavailable.");
    return;
  }

  std::string limitedTitle = cly::limits::truncateString(title, lim.maxStackTraceLineLength);
  std::string limitedTrace = cly::limits::truncateStackTrace(stackTrace, lim.maxStackTraceLinesPerThread, lim.maxStackTraceLineLength);
  std::map<std::string, std::string> limitedSegmentation = cly::limits::applySegmentationLimits(segmentation, lim);

  if (title.empty()) {
    impl->_logger->log(LogLevel::WARNING, "[Countly] [CrashModule] recordException, The parameter 'title' can't be empty");
  }

  if (stackTrace.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[Countly] [CrashModule] recordException, The parameter 'stackTrace' can't be empty");
  }

  // check if the crash metric '_os' exists and is not empty
  auto it = crashMetrics.find("_os");
  if (it == crashMetrics.end() || it->second.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[Countly] [CrashModule] recordException, The crash metric '_os' can't be empty");
  }

  // check if the crash metric '_app_version' exists and is not empty
  it = crashMetrics.find("_app_version");
  if (it == crashMetrics.end() || it->second.empty()) {
    impl->_logger->log(LogLevel::ERROR, "[Countly] [CrashModule] recordException, The crash metric '_app_version' can't be empty");
  }

  // lock mutex to avoid concurrent access; lock_guard releases on scope exit,
  // including when json construction / crash.dump() / addRequestToQueue throws.
  std::lock_guard<std::mutex> lk(*impl->_mutex);
  // convert breadcrumbs vector to a string and add to json object
  std::ostringstream outstream;
  std::copy(impl->_breadCrumbs.begin(), impl->_breadCrumbs.end(), std::ostream_iterator<std::string>(outstream, "\n"));

  // create json objects for crash metrics and segmentation
  nlohmann::json crash(crashMetrics);
  nlohmann::json segments(limitedSegmentation);

  // add relevant fields to the crash json object
  crash["_name"] = limitedTitle;
  crash["_error"] = limitedTrace;
  crash["_logs"] = outstream.str();
  crash["_custom"] = segments;
  crash["_nonfatal"] = !fatal;

  // create a map with the crash json object as value and "crash" as key, and add the map to the request queue
  std::map<std::string, std::string> data = {{"crash", crash.dump()}};
  impl->_requestModule->addRequestToQueue(data);
}

void CrashModule::setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider) { impl->_configProvider = std::move(provider); }

} // namespace cly