#include "countly/crash_module.hpp"
#include "countly/consent_module.hpp"
#include "countly/internal_limits.hpp"
#include "countly/request_module.hpp"
#include "native_crash.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <iostream>
#include <iterator>

// define namespace
namespace cly {
namespace {
// The device metrics a crash report carries
const char *const CRASH_METRIC_KEYS[] = {"_os", "_os_version", "_app_version", "_device", "_resolution"};
} // namespace

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
  // Whether the native crash handler is installed, so that every breadcrumb change is copied into its context block
  bool _nativeContext = false;
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

  /**
   * The internal limits in use, the defaults while start() has not run.
   */
  SDKLimits limits() {
    SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
    if (std::shared_ptr<ConfigurationProvider> config = _configProvider.lock()) {
      lim = config->getLimits();
    }
    return lim;
  }

  /**
   * Adds the device metrics a crash report carries to the report: "_os", "_os_version", "_app_version", "_device" and
   * "_resolution".
   */
  void addCrashMetrics(CrashData &crash) {
    const nlohmann::json deviceMetrics = _cly != nullptr ? _cly->DeviceMetrics() : nlohmann::json::object();
    for (const char *key : CRASH_METRIC_KEYS) {
      nlohmann::json::const_iterator metric = deviceMetrics.find(key);
      if (metric != deviceMetrics.end() && metric->is_string() && !metric->get<std::string>().empty()) {
        crash.crashMetrics[key] = metric->get<std::string>();
      }
    }
  }

  /**
   * Runs the crash filter on the report. Returns false when the filter drops the report or throws; 'changedFields'
   * gets the bits of the fields it changed, sent as "_ob".
   */
  bool runFilter(CrashData &crash, int &changedFields, const char *caller) {
    changedFields = 0;
    if (!_configuration->crashFilter) {
      return true;
    }
    // the stack trace is compared by size and hash, not kept as a copy, because a native crash dump makes it 13 MB large
    CrashData original;
    original.description = crash.description;
    original.crashSegmentation = crash.crashSegmentation;
    original.breadcrumbs = crash.breadcrumbs;
    original.crashMetrics = crash.crashMetrics;
    original.fatal = crash.fatal;
    original.nativeCrash = crash.nativeCrash;
    const size_t stackTraceSize = crash.stackTrace.size();
    const size_t stackTraceHash = std::hash<std::string>()(crash.stackTrace);
    bool drop = false;
    try {
      drop = _configuration->crashFilter(crash);
    } catch (...) {
      _logger->log(LogLevel::ERROR, std::string("[Countly] [CrashModule] ") + caller + ", The crash filter threw an exception, so the crash is dropped.");
      return false;
    }
    if (drop) {
      _logger->log(LogLevel::DEBUG, std::string("[Countly] [CrashModule] ") + caller + ", The crash filter dropped the crash.");
      return false;
    }
    crash.nativeCrash = original.nativeCrash;
    changedFields |= crash.description != original.description ? 64 : 0;
    changedFields |= crash.stackTrace.size() != stackTraceSize || std::hash<std::string>()(crash.stackTrace) != stackTraceHash ? 16 : 0;
    changedFields |= crash.crashSegmentation != original.crashSegmentation ? 8 : 0;
    changedFields |= crash.breadcrumbs != original.breadcrumbs ? 4 : 0;
    changedFields |= crash.crashMetrics != original.crashMetrics ? 2 : 0;
    changedFields |= crash.fatal != original.fatal ? 1 : 0;
    return true;
  }

  /**
   * Logs a warning for each metric the server needs that the report lacks.
   */
  void warnAboutMissingMetrics(const CrashData &crash, const char *caller) {
    for (const char *required : {"_os", "_app_version"}) {
      std::map<std::string, std::string>::const_iterator metric = crash.crashMetrics.find(required);
      if (metric == crash.crashMetrics.end() || metric->second.empty()) {
        _logger->log(LogLevel::WARNING, std::string("[Countly] [CrashModule] ") + caller + ", The crash metric '" + required + "' is missing, so the server will not accept the crash report. Give the app version with setMetrics().");
        _logger->recordLogCode("w700");
      }
    }
  }

  /**
   * Copies the crash-time metrics and the breadcrumbs into the context block that the native crash handler's dumps
   * contain. The oldest breadcrumbs are left out when the block can't hold them all. The caller holds _mutex.
   */
  void publishNativeContext() {
    if (!_nativeContext) {
      return;
    }
    CrashData metrics;
    addCrashMetrics(metrics);
    nlohmann::json context;
    context["m"] = nlohmann::json(metrics.crashMetrics);
    context["l"] = nlohmann::json::array();
    // the size of the payload grows by each breadcrumb's JSON text and a comma, so the newest that fit are found
    // without a full conversion for each breadcrumb
    size_t size = context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size();
    std::deque<std::string>::const_reverse_iterator oldestKept = _breadCrumbs.crbegin();
    for (; oldestKept != _breadCrumbs.crend(); ++oldestKept) {
      const size_t added = nlohmann::json(*oldestKept).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size() + (oldestKept == _breadCrumbs.crbegin() ? 0 : 1);
      if (size + added > native_crash::maxContextPayload()) {
        break;
      }
      size += added;
    }
    context["l"] = nlohmann::json(std::vector<std::string>(oldestKept.base(), _breadCrumbs.cend()));
    native_crash::setContextPayload(context.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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
  impl->publishNativeContext();
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
  impl->addCrashMetrics(crash);
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
    impl->publishNativeContext();
  }
  limitCrash(crash, lim, impl->_logger.get());

  int changedFields = 0;
  if (!impl->runFilter(crash, changedFields, "recordException")) {
    return;
  }
  if (impl->_configuration->crashFilter) {
    limitCrash(crash, lim, impl->_logger.get());
  }
  impl->warnAboutMissingMetrics(crash, "recordException");

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
  impl->publishNativeContext();
}

void CrashModule::setNativeContextEnabled(bool enabled) {
  std::lock_guard<std::mutex> lk(*impl->_mutex);
  impl->_nativeContext = enabled;
  if (enabled) {
    impl->publishNativeContext();
  } else {
    native_crash::setContextPayload(std::string());
  }
}

void CrashModule::recordNativeCrash(std::string dump) {
  impl->_logger->log(LogLevel::INFO, "[Countly] [CrashModule] recordNativeCrash, size = [" + std::to_string(dump.size()) + "] bytes");
  if (!impl->isInitialized("recordNativeCrash")) {
    return;
  }
  const SDKLimits lim = impl->limits();

  CrashData crash;
  crash.fatal = true;
  crash.nativeCrash = true;
  impl->addCrashMetrics(crash);
  std::string payload;
  const bool hasContext = native_crash::findContextPayload(dump, payload);
  crash.stackTrace = native_crash::base64Encode(dump);
  std::string().swap(dump);
  if (hasContext) {
    const nlohmann::json context = nlohmann::json::parse(payload, nullptr, false);
    if (context.is_object() && context.contains("m") && context["m"].is_object()) {
      for (const char *key : CRASH_METRIC_KEYS) {
        nlohmann::json::const_iterator metric = context["m"].find(key);
        if (metric != context["m"].end() && metric->is_string() && !metric->get<std::string>().empty()) {
          crash.crashMetrics[key] = metric->get<std::string>();
        }
      }
    }
    if (context.is_object() && context.contains("l") && context["l"].is_array()) {
      for (const nlohmann::json &breadcrumb : context["l"]) {
        if (breadcrumb.is_string()) {
          crash.breadcrumbs.push_back(breadcrumb.get<std::string>());
        }
      }
    }
  }
  crash.crashSegmentation = cly::limits::limitSegmentation(impl->_configuration->crashSegmentation, lim, impl->_logger.get(), "[CrashModule] recordNativeCrash");

  int changedFields = 0;
  if (!impl->runFilter(crash, changedFields, "recordNativeCrash")) {
    return;
  }
  crash.crashSegmentation = cly::limits::limitSegmentation(crash.crashSegmentation, lim, impl->_logger.get(), "[CrashModule] recordNativeCrash");
  impl->warnAboutMissingMetrics(crash, "recordNativeCrash");

  std::ostringstream outstream;
  std::copy(crash.breadcrumbs.begin(), crash.breadcrumbs.end(), std::ostream_iterator<std::string>(outstream, "\n"));

  nlohmann::json report(crash.crashMetrics);
  if (!crash.description.empty()) {
    report["_name"] = crash.description;
  }
  report["_error"] = std::move(crash.stackTrace);
  report["_logs"] = outstream.str();
  report["_custom"] = nlohmann::json(crash.crashSegmentation);
  report["_nonfatal"] = !crash.fatal;
  report["_native_cpp"] = true;
  report["_ob"] = changedFields;

  // the encoded dump moves into the report and the report text into the request data, so that no copy stays behind
  // cppcheck-suppress misra-c2012-12.3 ; the comma separates template arguments, it is no comma operator
  std::map<std::string, std::string> data;
  data["crash"] = report.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  report = nullptr;
  std::lock_guard<std::mutex> lk(*impl->_mutex);
  impl->_requestModule->addRequestToQueue(data);
}

} // namespace cly