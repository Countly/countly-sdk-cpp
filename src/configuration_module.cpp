#include "countly/configuration_module.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <thread>

namespace cly {
// some keys do not have feature yet, but reserved for future use.
static constexpr const char *KEY_TIMESTAMP = "t"; // not used yet
static constexpr const char *KEY_CONFIG = "c";    // used
static constexpr const char *KEY_VERSION = "v";   // not used yet

static constexpr const char *KEY_TRACKING = "tracking";
static constexpr const char *KEY_NETWORKING = "networking";

static constexpr const char *KEY_REQ_QUEUE_SIZE = "rqs";
static constexpr const char *KEY_EVENT_QUEUE_SIZE = "eqs";
static constexpr const char *KEY_SESSION_UPDATE_INTERVAL = "sui";
static constexpr const char *KEY_SESSION_TRACKING = "st";
static constexpr const char *KEY_VIEW_TRACKING = "vt";
static constexpr const char *KEY_LOCATION_TRACKING = "lt";
static constexpr const char *KEY_CUSTOM_EVENT_TRACKING = "cet";
static constexpr const char *KEY_CRASH_REPORTING = "crt";
static constexpr const char *KEY_CONSENT_REQUIRED = "cr";
static constexpr const char *KEY_SERVER_CONFIG_UPDATE_INTERVAL = "scui";
static constexpr const char *KEY_LOGGING = "log"; // not used and implemented yet

// whitelist / blacklist
static constexpr const char *KEY_EVENT_BLACKLIST = "eb";
static constexpr const char *KEY_USER_PROPERTY_BLACKLIST = "upb";
static constexpr const char *KEY_SEGMENTATION_BLACKLIST = "sb";
static constexpr const char *KEY_EVENT_SEGMENTATION_BLACKLIST = "esb";
static constexpr const char *KEY_EVENT_WHITELIST = "ew";
static constexpr const char *KEY_USER_PROPERTY_WHITELIST = "upw";
static constexpr const char *KEY_SEGMENTATION_WHITELIST = "sw";
static constexpr const char *KEY_EVENT_SEGMENTATION_WHITELIST = "esw";

// sdk configuration - not implemented yet
static constexpr const char *KEY_DROP_OLD_REQUEST_TIME = "dort";

// sdk internal limits - not implemented yet
static constexpr const char *KEY_LIMIT_KEY_LENGTH = "lkl";
static constexpr const char *KEY_LIMIT_VALUE_SIZE = "lvs";
static constexpr const char *KEY_LIMIT_SEG_VALUES = "lsv";
static constexpr const char *KEY_LIMIT_BREADCRUMB = "lbc";
static constexpr const char *KEY_LIMIT_TRACE_LINE = "ltlpt";
static constexpr const char *KEY_LIMIT_TRACE_LENGTH = "ltl";
// -- This limit is introduced lately and experimental
static constexpr const char *KEY_USER_PROPERTY_CACHE_LIMIT = "upcl";

// backoff mechanism - not implemented yet
static constexpr const char *KEY_BACKOFF_MECHANISM = "bom";
static constexpr const char *KEY_BOM_ACCEPTED_TIMEOUT = "bom_at";
static constexpr const char *KEY_BOM_RQ_PERCENTAGE = "bom_rqp";
static constexpr const char *KEY_BOM_REQUEST_AGE = "bom_ra";
static constexpr const char *KEY_BOM_DURATION = "bom_d";

class ConfigurationModule::ConfigurationModuleImpl {
private:
  std::shared_ptr<RequestBuilder> _requestBuilder;
  std::shared_ptr<StorageModuleBase> _storageModule;
  std::shared_ptr<RequestModule> _requestModule;
  cly::CountlyDelegates *_cly;
  nlohmann::json sdk_behavior_settings;

public:
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<std::mutex> _mutex;
  std::shared_ptr<CountlyConfiguration> _configuration;

  std::atomic<bool> stopConfigThread{false};
  std::thread configUpdateThread;
  std::mutex configUpdateMutex;
  std::condition_variable configUpdateCv;

  // current settings cached for quick access
  std::atomic<bool> networkingEnabled{true};
  std::atomic<bool> trackingEnabled{true};
  std::atomic<bool> sessionTrackingEnabled{true};
  std::atomic<bool> viewTrackingEnabled{true};
  std::atomic<bool> locationTrackingEnabled{true};
  std::atomic<bool> customEventTrackingEnabled{true};
  std::atomic<bool> crashReportingEnabled{true};
  // -1 while the settings don't contain "cr", otherwise 0 or 1
  std::atomic<int> consentRequired{-1};
  std::atomic<unsigned int> eventQueueThreshold{0};
  std::atomic<unsigned int> requestQueueSizeLimit{0};
  std::atomic<unsigned int> sessionUpdateInterval{0};
  std::atomic<unsigned int> serverConfigUpdateInterval{4};

  std::atomic<unsigned int> maxKeyLength{COUNTLY_MAX_KEY_LENGTH_DEFAULT};
  std::atomic<unsigned int> maxValueSize{COUNTLY_MAX_VALUE_SIZE_DEFAULT};
  std::atomic<unsigned int> maxSegmentationValues{COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT};
  std::atomic<unsigned int> maxBreadcrumbCount{COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT};
  std::atomic<unsigned int> maxStackTraceLinesPerThread{COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT};
  std::atomic<unsigned int> maxStackTraceLineLength{COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};

  std::atomic<unsigned int> requestDropAgeHours{0};
  std::atomic<bool> backoffEnabled{true};
  std::atomic<unsigned int> backoffAcceptedTimeout{10};
  std::atomic<double> backoffRequestQueuePercentage{0.5};
  std::atomic<unsigned int> backoffRequestAge{24};
  std::atomic<unsigned int> backoffDuration{60};

  mutable std::mutex sbsMutex;
  std::thread configFetchThread;
  std::atomic<bool> fetchRunning{false};
  std::atomic<bool> fetchAgain{false};
  // -1 while the settings don't contain "lt", otherwise 0 or 1
  std::atomic<int> serverLocationTracking{-1};

  mutable std::mutex filterMutex;
  FilterList<std::set<std::string>> eventFilter;
  FilterList<std::set<std::string>> userPropertyFilter;
  FilterList<std::set<std::string>> segmentationFilter;
  FilterList<std::map<std::string, std::set<std::string>>> eventSegmentationFilter;

  ConfigurationModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule,
                          std::shared_ptr<std::mutex> mutex)
      : _configuration(config), _logger(logger), _requestBuilder(requestBuilder), _storageModule(storageModule), _requestModule(requestModule), _mutex(mutex), _cly(cly) {}

  std::set<std::string> parseStringArray(const nlohmann::json &arr) const {
    std::set<std::string> result;
    if (arr.is_array()) {
      for (const auto &item : arr) {
        if (item.is_string()) {
          result.insert(item.get<std::string>());
        }
      }
    }
    return result;
  }

  std::map<std::string, std::set<std::string>> parseEventSegmentationMap(const nlohmann::json &obj) const {
    std::map<std::string, std::set<std::string>> result;
    if (obj.is_object()) {
      for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().is_array()) {
          result[it.key()] = parseStringArray(it.value());
        }
      }
    }
    return result;
  }

  // Helper to populate a list filter from blacklist/whitelist keys, reducing duplication
  template <typename T, typename ParseFunc>
  void populateListFilter(FilterList<T> &filter, const char *blacklistKey, const char *whitelistKey, ParseFunc parseFunc) {
    T blacklist = sdk_behavior_settings.contains(blacklistKey) ? parseFunc(sdk_behavior_settings[blacklistKey]) : T();
    T whitelist = sdk_behavior_settings.contains(whitelistKey) ? parseFunc(sdk_behavior_settings[whitelistKey]) : T();
    if (!blacklist.empty()) {
      filter.isWhitelist = false;
      filter.filterList = blacklist;
    } else if (!whitelist.empty()) {
      filter.isWhitelist = true;
      filter.filterList = whitelist;
    } else {
      filter.filterList.clear();
      filter.isWhitelist = false;
    }
  }

  /**
   * Fetches the settings for the device ID in use, and again while another fetch was asked for during it.
   */
  void _fetchLoop() {
    do {
      fetchAgain = false;
      _fetchConfigFromServerHTTP();
    } while (fetchAgain.load());
    fetchRunning = false;
  }

  /**
   * Whether the answer is a valid settings answer: a JSON object with a numeric "v", a numeric "t" and an object "c".
   */
  static bool isValidAnswer(const nlohmann::json &answer) {
    return answer.is_object() && answer.contains(KEY_VERSION) && answer[KEY_VERSION].is_number() && answer.contains(KEY_TIMESTAMP) && answer[KEY_TIMESTAMP].is_number() && answer.contains(KEY_CONFIG) && answer[KEY_CONFIG].is_object();
  }

  void _fetchConfigFromServerHTTP() {
    try {
      std::map<std::string, std::string> data;
      {
        std::lock_guard<std::mutex> mutexLock(*_mutex);
        data = {{"method", "sc"}, {"app_key", _configuration->appKey}, {"device_id", _configuration->deviceId}};
      }
      HTTPResponse response = _requestModule->sendImmediate("/o/sdk", _requestBuilder->buildRequest(data));
      if (response.success && isValidAnswer(response.data)) {
        nlohmann::json changedSettings;
        {
          std::lock_guard<std::mutex> lock(sbsMutex);
          nlohmann::json settings = response.data[KEY_CONFIG];
          sanitizeConfig(settings);
          sdk_behavior_settings = settings;
          _storageModule->storeSDKBehaviorSettings(sdk_behavior_settings.dump());
          _logger->log(LogLevel::INFO, "[Countly] [ConfigurationModule] _fetchConfigFromServerHTTP, SDK config:\n" + sdk_behavior_settings.dump(2));
          changedSettings = _populateConfigValues();
        }
        _onSBSChanged(changedSettings);
      } else {
        _logger->log(LogLevel::DEBUG, "[Countly] [ConfigurationModule] _fetchConfigFromServerHTTP, the settings could not be fetched, the ones in use stay. success=" + std::string(response.success ? "true" : "false") + ", response=" + response.data.dump());
      }
    } catch (const std::exception &e) {
      _logger->log(LogLevel::ERROR, "[Countly] [ConfigurationModule] _fetchConfigFromServerHTTP, exception: [" + std::string(e.what()) + "]");
    }
  }

  // Returns changed settings JSON for the caller to pass to _onSBSChanged outside of _mutex
  nlohmann::json _initializeSBSFromStorage() {
    _initializeConfigParameters();
    std::string sbs_string = _storageModule->getSDKBehaviorSettings();
    if (!sbs_string.empty()) {
      nlohmann::json changed = _processSDKBehaviorSettings(sbs_string);
      _logger->log(LogLevel::INFO, "[Countly] [ConfigurationModule] _initializeSBSFromStorage, initialized SDK behavior settings from storage.");
      return changed;
    } else if (!_configuration->sdkBehaviorSettings.empty()) {
      const nlohmann::json provided = nlohmann::json::parse(_configuration->sdkBehaviorSettings, nullptr, false);
      if (provided.is_discarded() || !provided.is_object()) {
        _logger->log(LogLevel::WARNING, "[Countly] [ConfigurationModule] _initializeSBSFromStorage, The provided SDK behavior settings are not a JSON object and are ignored.");
        return nlohmann::json{};
      }
      const nlohmann::json settings = provided.contains(KEY_CONFIG) && provided[KEY_CONFIG].is_object() ? provided[KEY_CONFIG] : provided;
      nlohmann::json changed = _processSDKBehaviorSettings(settings.dump());
      _logger->log(LogLevel::INFO, "[Countly] [ConfigurationModule] _initializeSBSFromStorage, initialized SDK behavior settings from configuration.");
      return changed;
    }
    return nlohmann::json{};
  }

  void _initializeConfigParameters() {
    requestQueueSizeLimit.store(_configuration->requestQueueThreshold, std::memory_order_release);
    sessionUpdateInterval.store(_configuration->sessionDuration, std::memory_order_release);

    maxKeyLength.store(_configuration->maxKeyLength, std::memory_order_release);
    maxValueSize.store(_configuration->maxValueSize, std::memory_order_release);
    maxSegmentationValues.store(_configuration->maxSegmentationValues, std::memory_order_release);
    maxBreadcrumbCount.store(_configuration->breadcrumbsThreshold, std::memory_order_release);
    maxStackTraceLinesPerThread.store(_configuration->maxStackTraceLinesPerThread, std::memory_order_release);
    maxStackTraceLineLength.store(_configuration->maxStackTraceLineLength, std::memory_order_release);

    requestDropAgeHours.store(_configuration->requestDropAgeHours, std::memory_order_release);
    backoffEnabled.store(_configuration->backoffMechanismEnabled, std::memory_order_release);
  }

  nlohmann::json _processSDKBehaviorSettings(const std::string &settings) {
    try {
      std::lock_guard<std::mutex> lock(sbsMutex);
      nlohmann::json sbs_json = nlohmann::json::parse(settings);
      sanitizeConfig(sbs_json);
      sdk_behavior_settings = sbs_json;
      _logger->log(LogLevel::INFO, "[Countly] [ConfigurationModule] _processSDKBehaviorSettings, SDK config:\n" + sdk_behavior_settings.dump(2));
      return _populateConfigValues();
    } catch (const nlohmann::json::parse_error &e) {
      _logger->log(LogLevel::ERROR, "[Countly] [ConfigurationModule] _processSDKBehaviorSettings, Failed to parse SDK behavior settings: [" + std::string(e.what()) + "]");
      return nlohmann::json{};
    }
  }

  void _onSBSChanged(const nlohmann::json &changedSettings, const nlohmann::json &session_params = nullptr) {
    if (_configuration->sdkBehaviorSettingsUpdatesDisabled != true && changedSettings.contains(KEY_SERVER_CONFIG_UPDATE_INTERVAL)) {
      // Wake the timer thread so it picks up the new interval on next iteration.
      // The timer loop re-reads serverConfigUpdateInterval (atomic) each cycle,
      // so we just need to interrupt the current wait.
      configUpdateCv.notify_all();
    }

    if (changedSettings.contains(KEY_CONSENT_REQUIRED)) {
      _cly->ConsentRequirementChanged();
    }

    if (changedSettings.contains(KEY_LOCATION_TRACKING)) {
      // safe because _onSBSChanged is called outside of _mutex
      _cly->LocationSettingChanged();
    }
  }

  nlohmann::json _populateConfigValues() {
    unsigned int serverConfigUpdateIntervalVal = serverConfigUpdateInterval.load(std::memory_order_acquire);

    bool locationTrackingCurrent = getBool(KEY_LOCATION_TRACKING, true);
    const int previousServerLocation = serverLocationTracking.load(std::memory_order_acquire);
    int serverLocation = -1;
    if (sdk_behavior_settings.is_object()) {
      nlohmann::json::const_iterator locationSetting = sdk_behavior_settings.find(KEY_LOCATION_TRACKING);
      if (locationSetting != sdk_behavior_settings.end() && locationSetting->is_boolean()) {
        serverLocation = locationSetting->get<bool>() ? 1 : 0;
      }
    }
    serverLocationTracking.store(serverLocation, std::memory_order_release);
    unsigned int serverConfigUpdateIntervalCurrent = getUInt(KEY_SERVER_CONFIG_UPDATE_INTERVAL, serverConfigUpdateIntervalVal);
    int consentRequiredVal = consentRequired.load(std::memory_order_acquire);
    int consentRequiredCurrent = -1;
    if (sdk_behavior_settings.is_object()) {
      auto consentRequiredSetting = sdk_behavior_settings.find(KEY_CONSENT_REQUIRED);
      if (consentRequiredSetting != sdk_behavior_settings.end() && consentRequiredSetting->is_boolean()) {
        consentRequiredCurrent = consentRequiredSetting->get<bool>() ? 1 : 0;
      }
    }
    consentRequired.store(consentRequiredCurrent, std::memory_order_release);

    trackingEnabled.store(getBool(KEY_TRACKING, true), std::memory_order_release);
    networkingEnabled.store(getBool(KEY_NETWORKING, true), std::memory_order_release);
    sessionTrackingEnabled.store(getBool(KEY_SESSION_TRACKING, true), std::memory_order_release);
    viewTrackingEnabled.store(getBool(KEY_VIEW_TRACKING, true), std::memory_order_release);
    locationTrackingEnabled.store(locationTrackingCurrent, std::memory_order_release);
    customEventTrackingEnabled.store(getBool(KEY_CUSTOM_EVENT_TRACKING, true), std::memory_order_release);
    crashReportingEnabled.store(getBool(KEY_CRASH_REPORTING, true), std::memory_order_release);
    eventQueueThreshold.store(getUInt(KEY_EVENT_QUEUE_SIZE, 0), std::memory_order_release);

    unsigned int rqs = getUInt(KEY_REQ_QUEUE_SIZE, _configuration->requestQueueThreshold);
    if (rqs < 1) {
      rqs = _configuration->requestQueueThreshold;
    }
    requestQueueSizeLimit.store(rqs, std::memory_order_release);

    unsigned int sui = getUInt(KEY_SESSION_UPDATE_INTERVAL, _configuration->sessionDuration);
    if (sui < 1) {
      sui = _configuration->sessionDuration;
    }
    sessionUpdateInterval.store(sui, std::memory_order_release);

    unsigned int scui = getUInt(KEY_SERVER_CONFIG_UPDATE_INTERVAL, 4);
    if (scui < 1) {
      scui = 4;
    }
    serverConfigUpdateInterval.store(scui, std::memory_order_release);

    // Parse listing filters — blacklist takes precedence over whitelist for each type
    {
      std::lock_guard<std::mutex> lock(filterMutex);
      auto parseArray = [this](const nlohmann::json &j) { return parseStringArray(j); };
      auto parseMap = [this](const nlohmann::json &j) { return parseEventSegmentationMap(j); };
      populateListFilter(eventFilter, KEY_EVENT_BLACKLIST, KEY_EVENT_WHITELIST, parseArray);
      populateListFilter(userPropertyFilter, KEY_USER_PROPERTY_BLACKLIST, KEY_USER_PROPERTY_WHITELIST, parseArray);
      populateListFilter(segmentationFilter, KEY_SEGMENTATION_BLACKLIST, KEY_SEGMENTATION_WHITELIST, parseArray);
      populateListFilter(eventSegmentationFilter, KEY_EVENT_SEGMENTATION_BLACKLIST, KEY_EVENT_SEGMENTATION_WHITELIST, parseMap);
    }

    auto readLimit = [this](const char *key, unsigned int def) {
      unsigned int v = getUInt(key, def);
      return v < 1 ? def : v;
    };
    maxKeyLength.store(readLimit(KEY_LIMIT_KEY_LENGTH, _configuration->maxKeyLength), std::memory_order_release);
    maxValueSize.store(readLimit(KEY_LIMIT_VALUE_SIZE, _configuration->maxValueSize), std::memory_order_release);
    maxSegmentationValues.store(readLimit(KEY_LIMIT_SEG_VALUES, _configuration->maxSegmentationValues), std::memory_order_release);
    maxBreadcrumbCount.store(readLimit(KEY_LIMIT_BREADCRUMB, _configuration->breadcrumbsThreshold), std::memory_order_release);
    maxStackTraceLinesPerThread.store(readLimit(KEY_LIMIT_TRACE_LINE, _configuration->maxStackTraceLinesPerThread), std::memory_order_release);
    maxStackTraceLineLength.store(readLimit(KEY_LIMIT_TRACE_LENGTH, _configuration->maxStackTraceLineLength), std::memory_order_release);

    requestDropAgeHours.store(getUInt(KEY_DROP_OLD_REQUEST_TIME, _configuration->requestDropAgeHours), std::memory_order_release);
    backoffEnabled.store(getBool(KEY_BACKOFF_MECHANISM, _configuration->backoffMechanismEnabled), std::memory_order_release);
    backoffAcceptedTimeout.store(readLimit(KEY_BOM_ACCEPTED_TIMEOUT, 10), std::memory_order_release);
    backoffRequestAge.store(readLimit(KEY_BOM_REQUEST_AGE, 24), std::memory_order_release);
    backoffDuration.store(readLimit(KEY_BOM_DURATION, 60), std::memory_order_release);
    double queuePercentage = 0.5;
    if (sdk_behavior_settings.is_object()) {
      nlohmann::json::const_iterator setting = sdk_behavior_settings.find(KEY_BOM_RQ_PERCENTAGE);
      if (setting != sdk_behavior_settings.end() && setting->is_number()) {
        queuePercentage = setting->get<double>();
      }
    }
    backoffRequestQueuePercentage.store(queuePercentage, std::memory_order_release);

    nlohmann::json changedSettings;
    if (serverLocation != previousServerLocation) {
      changedSettings[KEY_LOCATION_TRACKING] = serverLocation;
    }
    if (serverConfigUpdateIntervalCurrent != serverConfigUpdateIntervalVal) {
      changedSettings[KEY_SERVER_CONFIG_UPDATE_INTERVAL] = serverConfigUpdateIntervalCurrent;
    }
    if (consentRequiredCurrent != consentRequiredVal) {
      changedSettings[KEY_CONSENT_REQUIRED] = consentRequiredCurrent;
    }
    return changedSettings;
  }

  void sanitizeConfig(nlohmann::json &c) {
    if (!c.is_object()) {
      c.clear();
      return;
    }

    for (auto it = c.begin(); it != c.end();) {
      std::string key = it.key();
      auto value = it.value();
      bool known = true;
      bool valid = true;
      if (key == KEY_REQ_QUEUE_SIZE || key == KEY_EVENT_QUEUE_SIZE || key == KEY_SESSION_UPDATE_INTERVAL || key == KEY_LIMIT_KEY_LENGTH || key == KEY_LIMIT_VALUE_SIZE || key == KEY_LIMIT_SEG_VALUES || key == KEY_LIMIT_BREADCRUMB || key == KEY_LIMIT_TRACE_LINE || key == KEY_LIMIT_TRACE_LENGTH ||
          key == KEY_USER_PROPERTY_CACHE_LIMIT || key == KEY_SERVER_CONFIG_UPDATE_INTERVAL || key == KEY_BOM_ACCEPTED_TIMEOUT || key == KEY_BOM_REQUEST_AGE || key == KEY_BOM_DURATION) {
        valid = value.is_number_unsigned() && value.get<unsigned long long>() >= 1 && value.get<unsigned long long>() <= UINT_MAX;
      } else if (key == KEY_DROP_OLD_REQUEST_TIME) {
        valid = value.is_number_unsigned() && value.get<unsigned long long>() <= UINT_MAX;
      } else if (key == KEY_BOM_RQ_PERCENTAGE) {
        valid = value.is_number() && value.get<double>() > 0 && value.get<double>() <= 1;
      } else if (key == KEY_TRACKING || key == KEY_NETWORKING || key == KEY_LOGGING || key == KEY_SESSION_TRACKING || key == KEY_VIEW_TRACKING || key == KEY_LOCATION_TRACKING || key == KEY_CUSTOM_EVENT_TRACKING || key == KEY_CONSENT_REQUIRED || key == KEY_CRASH_REPORTING || key == KEY_BACKOFF_MECHANISM) {
        valid = value.is_boolean();
      } else if (key == KEY_EVENT_BLACKLIST || key == KEY_USER_PROPERTY_BLACKLIST || key == KEY_SEGMENTATION_BLACKLIST || key == KEY_EVENT_WHITELIST || key == KEY_USER_PROPERTY_WHITELIST || key == KEY_SEGMENTATION_WHITELIST) {
        valid = value.is_array();
      } else if (key == KEY_EVENT_SEGMENTATION_BLACKLIST || key == KEY_EVENT_SEGMENTATION_WHITELIST) {
        valid = value.is_object();
      } else {
        known = false;
      }

      if (!known) {
        _logger->log(LogLevel::DEBUG, "[Countly] [ConfigurationModule] sanitizeConfig, removing unknown key: [" + key + "]");
        it = c.erase(it);
        continue;
      }
      if (!valid) {
        _logger->log(LogLevel::WARNING, "[Countly] [ConfigurationModule] sanitizeConfig, The SDK behavior setting [" + key + "] has an invalid value [" + value.dump() + "] and is ignored.");
        _logger->recordLogCode("w121");
        it = c.erase(it);
        continue;
      }
      ++it;
    }
  }

  // Lock ordering: _mutex -> sbsMutex -> filterMutex (must never be reversed)
  // configUpdateMutex is only used by the timer thread and _stopTimer; never held while acquiring _mutex.
  void _updateConfigPeriodically(const nlohmann::json &session_params) {
    std::unique_lock<std::mutex> lock(configUpdateMutex);

    while (!stopConfigThread.load(std::memory_order_acquire)) {
      try {
        unsigned int interval = serverConfigUpdateInterval.load(std::memory_order_acquire);

        if (interval < 1) {
          interval = 4;
        }

        // about 11 years at most, so that a huge interval can't overflow the clock
        auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(std::min(interval, 100000u));
        bool stopped = configUpdateCv.wait_until(lock, deadline, [&] { return stopConfigThread.load(std::memory_order_acquire); });
        if (stopped) {
          return;
        }

        if (std::chrono::steady_clock::now() < deadline) {
          continue;
        }

        lock.unlock();
        _fetchConfigFromServerHTTP();
        lock.lock();
      } catch (const std::exception &e) {
        _logger->log(LogLevel::ERROR, "[Countly] [ConfigurationModule] _updateConfigPeriodically, exception: [" + std::string(e.what()) + "]");
        if (!lock.owns_lock()) {
          lock.lock();
        }
      }
    }
  }

  void _stopTimer() {
    _logger->log(LogLevel::DEBUG, "[Countly] [ConfigurationModule] stopTimer, stopping server config update timer thread.");

    {
      // The flag has to be mutated under the same mutex the waiter evaluates it
      // under. Storing it outside and then notifying can land in the window
      // between the waiter checking the predicate and actually blocking, in
      // which case the notification is lost -- and the wait deadline is four
      // hours, so join() below would block for that long.
      std::lock_guard<std::mutex> lk(configUpdateMutex);
      stopConfigThread.store(true, std::memory_order_release);
    }
    configUpdateCv.notify_all();

    if (configUpdateThread.joinable()) {
      configUpdateThread.join();
    }
  }

  void _startTimer(nlohmann::json session_params) {
    if (_configuration->sdkBehaviorSettingsUpdatesDisabled) {
      _logger->log(LogLevel::INFO, "[Countly] [ConfigurationModule] _startTimer, SDK behavior settings updates are disabled.");
      return;
    }

    if (configUpdateThread.joinable()) {
      return;
    }

    stopConfigThread.store(false, std::memory_order_release);
    configUpdateThread = std::thread(&ConfigurationModule::ConfigurationModuleImpl::_updateConfigPeriodically, this, session_params);
  }

  ~ConfigurationModuleImpl() {
    // Join the one-shot fetch thread. The HTTP client MUST have a timeout
    // configured, otherwise this will block destruction indefinitely.
    if (configFetchThread.joinable()) {
      configFetchThread.join();
    }
    _stopTimer();
    _logger.reset();
  }

  bool getBool(const char *key, bool defaultValue) const {
    if (!sdk_behavior_settings.is_object()) {
      return defaultValue;
    }

    auto it = sdk_behavior_settings.find(key);
    if (it == sdk_behavior_settings.end() || !it->is_boolean()) {
      return defaultValue;
    }

    bool value = it->get<bool>();
    return value;
  }

  unsigned int getUInt(const char *key, unsigned int defaultValue) const {
    if (!sdk_behavior_settings.is_object()) {
      return defaultValue;
    }

    auto it = sdk_behavior_settings.find(key);
    if (it == sdk_behavior_settings.end() || !it->is_number_unsigned()) {
      return defaultValue;
    }

    unsigned int value = it->get<unsigned int>();
    return value;
  }
};

ConfigurationModule::ConfigurationModule(cly::CountlyDelegates *cly, std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule,
                                         std::shared_ptr<std::mutex> mutex) {
  impl.reset(new ConfigurationModuleImpl(cly, config, logger, requestBuilder, storageModule, requestModule, mutex));
  impl->_logger->log(LogLevel::DEBUG, "[Countly] [ConfigurationModule] Initialized");
}

ConfigurationModule::~ConfigurationModule() { impl.reset(); }

void ConfigurationModule::fetchConfigFromServer(nlohmann::json session_params) {
  (void)session_params;
  if (impl->_configuration->sdkBehaviorSettingsUpdatesDisabled) {
    impl->_logger->log(LogLevel::DEBUG, "[Countly] [ConfigurationModule] fetchConfigFromServer, SDK behavior settings updates are disabled.");
    return;
  }
  if (impl->fetchRunning.exchange(true)) {
    impl->fetchAgain = true;
    return;
  }
  // the previous fetch thread has finished, so this returns at once
  if (impl->configFetchThread.joinable()) {
    impl->configFetchThread.join();
  }
  impl->configFetchThread = std::thread(&ConfigurationModule::ConfigurationModuleImpl::_fetchLoop, impl.get());
}

void ConfigurationModule::fetchConfigFromStorage() {
  nlohmann::json changedSettings;
  {
    std::lock_guard<std::mutex> lock(*impl->_mutex);
    changedSettings = impl->_initializeSBSFromStorage();
  }

  // Call _onSBSChanged outside of _mutex to avoid deadlock with RecordLocation
  if (!changedSettings.empty()) {
    impl->_onSBSChanged(changedSettings);
  }
}

void ConfigurationModule::startServerConfigUpdateTimer(nlohmann::json session_params) { impl->_startTimer(session_params); }

void ConfigurationModule::stopTimer() { impl->_stopTimer(); }

bool ConfigurationModule::isTrackingEnabled() const { return impl->trackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isNetworkingEnabled() const { return impl->networkingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isLocationTrackingEnabled() { return impl->locationTrackingEnabled.load(std::memory_order_acquire); }

int ConfigurationModule::getServerLocationTracking() const { return impl->serverLocationTracking.load(std::memory_order_acquire); }

bool ConfigurationModule::isViewTrackingEnabled() const { return impl->viewTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isSessionTrackingEnabled() { return impl->sessionTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isCustomEventTrackingEnabled() { return impl->customEventTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isCrashReportingEnabled() const { return impl->crashReportingEnabled.load(std::memory_order_acquire); }

int ConfigurationModule::getConsentRequired() const { return impl->consentRequired.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getRequestQueueSizeLimit() const { return impl->requestQueueSizeLimit.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getRequestDropAgeHours() const { return impl->requestDropAgeHours.load(std::memory_order_acquire); }

bool ConfigurationModule::isBackoffEnabled() const { return impl->backoffEnabled.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getBackoffAcceptedTimeout() const { return impl->backoffAcceptedTimeout.load(std::memory_order_acquire); }

double ConfigurationModule::getBackoffRequestQueuePercentage() const { return impl->backoffRequestQueuePercentage.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getBackoffRequestAge() const { return impl->backoffRequestAge.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getBackoffDuration() const { return impl->backoffDuration.load(std::memory_order_acquire); }

SDKLimits ConfigurationModule::getLimits() const {
  SDKLimits lim;
  lim.maxKeyLength = impl->maxKeyLength.load(std::memory_order_acquire);
  lim.maxValueSize = impl->maxValueSize.load(std::memory_order_acquire);
  lim.maxSegmentationValues = impl->maxSegmentationValues.load(std::memory_order_acquire);
  lim.maxBreadcrumbCount = impl->maxBreadcrumbCount.load(std::memory_order_acquire);
  lim.maxStackTraceLinesPerThread = impl->maxStackTraceLinesPerThread.load(std::memory_order_acquire);
  lim.maxStackTraceLineLength = impl->maxStackTraceLineLength.load(std::memory_order_acquire);
  return lim;
}

unsigned int ConfigurationModule::getEventQueueSizeLimit() {
    // this is because we permit EQ size to change after initialization
    unsigned int value = impl->eventQueueThreshold.load(std::memory_order_acquire);
    return value == 0 ? impl->_configuration->eventQueueThreshold : value;

}

unsigned int ConfigurationModule::getSessionUpdateInterval() { return impl->sessionUpdateInterval.load(std::memory_order_acquire); }

FilterList<std::set<std::string>> ConfigurationModule::getEventFilterList() const {
  std::lock_guard<std::mutex> lock(impl->filterMutex);
  return impl->eventFilter;
}

FilterList<std::set<std::string>> ConfigurationModule::getUserPropertyFilterList() const {
  std::lock_guard<std::mutex> lock(impl->filterMutex);
  return impl->userPropertyFilter;
}

FilterList<std::set<std::string>> ConfigurationModule::getSegmentationFilterList() const {
  std::lock_guard<std::mutex> lock(impl->filterMutex);
  return impl->segmentationFilter;
}

FilterList<std::map<std::string, std::set<std::string>>> ConfigurationModule::getEventSegmentationFilterList() const {
  std::lock_guard<std::mutex> lock(impl->filterMutex);
  return impl->eventSegmentationFilter;
}

// namespace cly
} // namespace cly