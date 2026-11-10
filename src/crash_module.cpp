#include "countly/crash_module.hpp"
#include "countly/consent_module.hpp"
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
  std::shared_ptr<ConsentModule> _consent;
  cly::CountlyDelegates *_cly = nullptr;
  CrashModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) : _configuration(config), _logger(logger), _requestModule(requestModule), _mutex(mutex) {}

  // destructor to reset logger
  ~CrashModuleImpl() { _logger.reset(); }

  /**
   * Whether start() has wired the module up, logged as a warning and counted as w101 when it has not.
   */
  bool isInitialized(const char *caller) {
    bool initialized = false;
    {
      std::lock_guard<std::mutex> lk(*_mutex);
      initialized = !_configProvider.expired() && _requestModule != nullptr;
    }
    if (initialized) {
      return true;
    }
    _logger->log(LogLevel::WARNING, std::string("[Countly] [CrashModule] ") + caller + ", SDK is not initialized.");
    _logger->recordLogCode("w101");
    return false;
  }

  /**
   * Whether the "crashes" consent allows the call, logged at debug level when it does not.
   */
  bool hasConsent(const char *caller) {
    if (!_consent || _consent->getConsent(consent::CRASHES)) {
      return true;
    }
    _logger->log(LogLevel::DEBUG, std::string("[Countly] [CrashModule] ") + caller + ", the 'crashes' consent is not given, the call is ignored.");
    return false;
  }
};

// destructor to reset implementation
CrashModule::~CrashModule() { impl.reset(); }

CrashModule::CrashModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) {
  impl.reset(new CrashModuleImpl(config, logger, requestModule, mutex));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [CrashModule] Initialized"));
}

// function to add breadcrumb
void CrashModule::addBreadcrumb(const std::string &value) {
  if (!impl->isInitialized("addBreadcrumb") || !impl->hasConsent("addBreadcrumb")) {
    return;
  }
  SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  if (std::shared_ptr<ConfigurationProvider> config = impl->_configProvider.lock()) {
    lim = config->getLimits();
  }

  std::string limited = cly::limits::limitValue(value, lim.maxValueSize, impl->_logger.get(), "[CrashModule] addBreadcrumb", "breadcrumb");
  impl->_logger->log(LogLevel::INFO, "[Countly] [CrashModule] addBreadcrumb, value = [" + limited + "]");
  impl->_logger->recordUsage("crashes", "log");

  std::lock_guard<std::mutex> lk(*impl->_mutex);
  // if breadcrumb count limit is reached, remove oldest breadcrumb
  if (impl->_breadCrumbs.size() >= lim.maxBreadcrumbCount) {
    impl->_breadCrumbs.pop_front();
  }
  // add new breadcrumb
  impl->_breadCrumbs.push_back(limited);
}

namespace {
/**
 * Applies the internal limits to a crash report: the title and the stack trace follow the stack trace limits, the
 * segmentation the key, value and entry limits, the breadcrumbs the value size and count limits.
 */
void limitCrash(CrashData &crash, const SDKLimits &lim, LoggerModule *logger) {
  crash.description = cly::limits::limitErrorText(crash.description, lim.maxStackTraceLineLength, logger, "[CrashModule] recordException");
  crash.stackTrace = cly::limits::limitStackTrace(crash.stackTrace, lim.maxStackTraceLinesPerThread, lim.maxStackTraceLineLength, logger, "[CrashModule] recordException");
  crash.crashSegmentation = cly::limits::limitSegmentation(crash.crashSegmentation, lim, logger, "[CrashModule] recordException");
  if (crash.breadcrumbs.size() > lim.maxBreadcrumbCount) {
    crash.breadcrumbs.erase(crash.breadcrumbs.begin(), crash.breadcrumbs.end() - lim.maxBreadcrumbCount);
  }
  for (std::string &breadcrumb : crash.breadcrumbs) {
    breadcrumb = cly::limits::limitValue(breadcrumb, lim.maxValueSize, logger, "[CrashModule] recordException", "breadcrumb");
  }
}
} // namespace

void CrashModule::recordException(const std::string &title, const std::string &stackTrace, const bool fatal, const std::map<std::string, std::string> &crashMetrics, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(LogLevel::INFO, cly::utils::format_string("[Countly] [CrashModule] recordException, title = [%s], stackTrace = [%s]", title.c_str(), stackTrace.c_str()));
  if (!impl->isInitialized("recordException") || !impl->hasConsent("recordException")) {
    return;
  }
  impl->_logger->recordUsage("crashes", "record");

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

  if (stackTrace.empty()) {
    impl->_logger->log(LogLevel::WARNING, "[Countly] [CrashModule] recordException, The parameter 'stackTrace' can't be empty, the crash is not recorded.");
    impl->_logger->recordLogCode("w700");
    return;
  }

  CrashData crash;
  crash.description = title;
  crash.stackTrace = stackTrace;
  crash.fatal = fatal;
  const nlohmann::json deviceMetrics = impl->_cly != nullptr ? impl->_cly->DeviceMetrics() : nlohmann::json::object();
  for (const char *key : {"_os", "_os_version", "_app_version", "_device", "_resolution"}) {
    nlohmann::json::const_iterator metric = deviceMetrics.find(key);
    if (metric != deviceMetrics.end() && metric->is_string() && !metric->get<std::string>().empty()) {
      crash.crashMetrics[key] = metric->get<std::string>();
    }
  }
  for (const auto &metric : crashMetrics) {
    crash.crashMetrics[metric.first] = metric.second;
  }
  crash.crashSegmentation = impl->_configuration->crashSegmentation;
  for (const auto &entry : segmentation) {
    crash.crashSegmentation[entry.first] = entry.second;
  }
  {
    // a processed crash takes the breadcrumbs, so the next crash report starts with a fresh trail
    std::lock_guard<std::mutex> lk(*impl->_mutex);
    crash.breadcrumbs.assign(impl->_breadCrumbs.begin(), impl->_breadCrumbs.end());
    impl->_breadCrumbs.clear();
  }
  limitCrash(crash, lim, impl->_logger.get());

  int changedFields = 0;
  if (impl->_configuration->crashFilter) {
    const CrashData original = crash;
    bool drop = false;
    try {
      drop = impl->_configuration->crashFilter(crash);
    } catch (...) {
      impl->_logger->log(LogLevel::ERROR, "[Countly] [CrashModule] recordException, The crash filter threw an exception, so the crash is dropped.");
      return;
    }
    if (drop) {
      impl->_logger->log(LogLevel::DEBUG, "[Countly] [CrashModule] recordException, The crash filter dropped the crash.");
      return;
    }
    changedFields |= crash.description != original.description ? 64 : 0;
    changedFields |= crash.stackTrace != original.stackTrace ? 16 : 0;
    changedFields |= crash.crashSegmentation != original.crashSegmentation ? 8 : 0;
    changedFields |= crash.breadcrumbs != original.breadcrumbs ? 4 : 0;
    changedFields |= crash.crashMetrics != original.crashMetrics ? 2 : 0;
    changedFields |= crash.fatal != original.fatal ? 1 : 0;
    limitCrash(crash, lim, impl->_logger.get());
  }

  for (const char *required : {"_os", "_app_version"}) {
    std::map<std::string, std::string>::const_iterator metric = crash.crashMetrics.find(required);
    if (metric == crash.crashMetrics.end() || metric->second.empty()) {
      impl->_logger->log(LogLevel::WARNING, std::string("[Countly] [CrashModule] recordException, The crash metric '") + required + "' is missing, so the server will not accept the crash report. Give the app version with setMetrics().");
      impl->_logger->recordLogCode("w700");
    }
  }

  std::ostringstream outstream;
  std::copy(crash.breadcrumbs.begin(), crash.breadcrumbs.end(), std::ostream_iterator<std::string>(outstream, "\n"));

  nlohmann::json report(crash.crashMetrics);
  report["_name"] = crash.description;
  report["_error"] = crash.stackTrace;
  report["_logs"] = outstream.str();
  report["_custom"] = nlohmann::json(crash.crashSegmentation);
  report["_nonfatal"] = !crash.fatal;
  report["_ob"] = changedFields;
  const std::string currentViewId = impl->_cly != nullptr ? impl->_cly->CurrentViewId() : std::string();
  if (!currentViewId.empty()) {
    report["_cvid"] = currentViewId;
  }

  // lock mutex to avoid concurrent access; lock_guard releases on scope exit,
  // including when json construction / crash.dump() / addRequestToQueue throws.
  std::lock_guard<std::mutex> lk(*impl->_mutex);
  std::map<std::string, std::string> data = {{"crash", report.dump()}};
  impl->_requestModule->addRequestToQueue(data);
}

void CrashModule::setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider) { impl->_configProvider = std::move(provider); }

void CrashModule::setConsentModule(std::shared_ptr<ConsentModule> consent) { impl->_consent = std::move(consent); }

void CrashModule::setRequestModule(std::shared_ptr<RequestModule> requestModule) { impl->_requestModule = std::move(requestModule); }

void CrashModule::setDelegates(cly::CountlyDelegates *cly) { impl->_cly = cly; }

void CrashModule::clearBreadcrumbs() {
  impl->_logger->log(LogLevel::INFO, "[Countly] [CrashModule] clearBreadcrumbs");
  if (!impl->isInitialized("clearBreadcrumbs") || !impl->hasConsent("clearBreadcrumbs")) {
    return;
  }
  impl->_logger->recordUsage("crashes", "clear");
  forgetBreadcrumbs();
}

void CrashModule::forgetBreadcrumbs() {
  std::lock_guard<std::mutex> lk(*impl->_mutex);
  impl->_breadCrumbs.clear();
}

} // namespace cly