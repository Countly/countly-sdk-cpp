#include "countly/configuration_module.hpp"
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
static constexpr const char *KEY_SERVER_CONFIG_UPDATE_INTERVAL = "scui";
static constexpr const char *KEY_LOGGING = "log"; // not used and implemented yet

// whitelist / blacklist - not implemented yet
static constexpr const char *KEY_EVENT_BLACKLIST = "eb";
static constexpr const char *KEY_USER_PROPERTY_BLACKLIST = "upb";
static constexpr const char *KEY_SEGMENTATION_BLACKLIST = "sb";
static constexpr const char *KEY_EVENT_SEGMENTATION_BLACKLIST = "esb";
static constexpr const char *KEY_EVENT_WHITELIST = "ew";
static constexpr const char *KEY_USER_PROPERTY_WHITELIST = "upw";
static constexpr const char *KEY_SEGMENTATION_WHITELIST = "sw";
static constexpr const char *KEY_EVENT_SEGMENTATION_WHITELIST = "esw";

// sdk configuration - not implemented yet
static constexpr const char *KEY_CONSENT_REQUIRED = "cr";
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
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<RequestBuilder> _requestBuilder;
  std::shared_ptr<StorageModuleBase> _storageModule;
  std::shared_ptr<RequestModule> _requestModule;
  cly::CountlyDelegates *_cly;
  nlohmann::json sdk_behavior_settings;

public:
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<std::mutex> _mutex;

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
  std::atomic<unsigned int> eventQueueThreshold{0};
  std::atomic<unsigned int> requestQueueSizeLimit{0};
  std::atomic<unsigned int> sessionUpdateInterval{0};
  std::atomic<unsigned int> serverConfigUpdateInterval{4};
  ConfigurationModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule,
                          std::shared_ptr<std::mutex> mutex)
      : _configuration(config), _logger(logger), _requestBuilder(requestBuilder), _storageModule(storageModule), _requestModule(requestModule), _mutex(mutex), _cly(cly) {}

  void _fetchConfigFromServerHTTP(const std::map<std::string, std::string> &data, const nlohmann::json &session_params) {
    HTTPResponse response = _requestModule->sendHTTP("/o/sdk", _requestBuilder->serializeData(data));
    if (response.success && response.data.is_object() && response.data.contains(KEY_CONFIG)) {
      sanitizeConfig(response.data[KEY_CONFIG]);
      sdk_behavior_settings = response.data[KEY_CONFIG];
      _storageModule->storeSDKBehaviorSettings(sdk_behavior_settings.dump());
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _fetchConfigFromServerHTTP, SDK config:\n" + sdk_behavior_settings.dump(2));
      _onSBSChanged(_populateConfigValues(), session_params);
    } else {
      _logger->log(LogLevel::WARNING, cly::utils::format_string("[ConfigurationModule] _fetchConfigFromServerHTTP, failed to fetch response_success: [%s]", response.success ? "true" : "false"));
    }
  }

  void _initializeSBSFromStorage() {
    std::string sbs_string = _storageModule->getSDKBehaviorSettings();
    if (!sbs_string.empty()) {
      _processSDKBehaviorSettings(sbs_string);
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _initializeSBSFromStorage, initialized SDK behavior settings from storage.");
    } else if (!_configuration->sdkBehaviorSettings.empty()) {
      _onSBSChanged(_processSDKBehaviorSettings(_configuration->sdkBehaviorSettings));
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _initializeSBSFromStorage, initialized SDK behavior settings from configuration.");
    }
  }

  nlohmann::json _processSDKBehaviorSettings(const std::string &settings) {
    try {
      nlohmann::json sbs_json = nlohmann::json::parse(settings);
      sanitizeConfig(sbs_json);
      sdk_behavior_settings = sbs_json;
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _processSDKBehaviorSettings, SDK config:\n" + sdk_behavior_settings.dump(2));
      return _populateConfigValues();
    } catch (const nlohmann::json::parse_error &e) {
      _logger->log(LogLevel::ERROR, "[ConfigurationModule] _processSDKBehaviorSettings, Failed to parse SDK behavior settings: " + std::string(e.what()));
      return nlohmann::json{};
    }
  }

  void _onSBSChanged(const nlohmann::json &changedSettings, const nlohmann::json &session_params = nullptr) {
    if (_configuration->sdkBehaviorSettingsUpdatesDisabled != true && changedSettings.contains(KEY_SERVER_CONFIG_UPDATE_INTERVAL)) {
      // restart timer with new interval
      _stopTimer();
      _startTimer(session_params);
    }

    if (changedSettings.contains(KEY_LOCATION_TRACKING) && changedSettings[KEY_LOCATION_TRACKING] == false) {
      // disable location
      _cly->RecordLocation("", "", "", "");
    }
  }

  nlohmann::json _populateConfigValues() {
    bool trackingEnabledVal = trackingEnabled.load(std::memory_order_acquire);
    bool networkingEnabledVal = networkingEnabled.load(std::memory_order_acquire);
    bool sessionTrackingEnabledVal = sessionTrackingEnabled.load(std::memory_order_acquire);
    bool viewTrackingEnabledVal = viewTrackingEnabled.load(std::memory_order_acquire);
    bool locationTrackingEnabledVal = locationTrackingEnabled.load(std::memory_order_acquire);
    bool customEventTrackingEnabledVal = customEventTrackingEnabled.load(std::memory_order_acquire);
    bool crashReportingEnabledVal = crashReportingEnabled.load(std::memory_order_acquire);
    int unsigned serverConfigUpdateIntervalVal = serverConfigUpdateInterval.load(std::memory_order_acquire);

    bool locationTrackingCurrent = getBool(KEY_LOCATION_TRACKING, locationTrackingEnabledVal);
    int unsigned serverConfigUpdateIntervalCurrent = getUInt(KEY_SERVER_CONFIG_UPDATE_INTERVAL, serverConfigUpdateIntervalVal);

    trackingEnabled.store(getBool(KEY_TRACKING, trackingEnabledVal), std::memory_order_release);
    networkingEnabled.store(getBool(KEY_NETWORKING, networkingEnabledVal), std::memory_order_release);
    sessionTrackingEnabled.store(getBool(KEY_SESSION_TRACKING, sessionTrackingEnabledVal), std::memory_order_release);
    viewTrackingEnabled.store(getBool(KEY_VIEW_TRACKING, viewTrackingEnabledVal), std::memory_order_release);
    locationTrackingEnabled.store(locationTrackingCurrent, std::memory_order_release);
    customEventTrackingEnabled.store(getBool(KEY_CUSTOM_EVENT_TRACKING, customEventTrackingEnabledVal), std::memory_order_release);
    crashReportingEnabled.store(getBool(KEY_CRASH_REPORTING, crashReportingEnabledVal), std::memory_order_release);
    eventQueueThreshold.store(getUInt(KEY_EVENT_QUEUE_SIZE, _configuration->eventQueueThreshold), std::memory_order_release);
    requestQueueSizeLimit.store(getUInt(KEY_REQ_QUEUE_SIZE, _configuration->requestQueueThreshold), std::memory_order_release);
    sessionUpdateInterval.store(getUInt(KEY_SESSION_UPDATE_INTERVAL, _configuration->sessionDuration), std::memory_order_release);
    serverConfigUpdateInterval.store(getUInt(KEY_SERVER_CONFIG_UPDATE_INTERVAL, 4), std::memory_order_release);

    nlohmann::json changedSettings;
    if (locationTrackingCurrent != locationTrackingEnabledVal) {
      changedSettings[KEY_LOCATION_TRACKING] = locationTrackingCurrent;
    }
    if (serverConfigUpdateIntervalCurrent != serverConfigUpdateIntervalVal) {
      changedSettings[KEY_SERVER_CONFIG_UPDATE_INTERVAL] = serverConfigUpdateIntervalCurrent;
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
      if (key == KEY_REQ_QUEUE_SIZE || key == KEY_EVENT_QUEUE_SIZE || key == KEY_SESSION_UPDATE_INTERVAL || key == KEY_LIMIT_KEY_LENGTH || key == KEY_LIMIT_VALUE_SIZE || key == KEY_LIMIT_SEG_VALUES || key == KEY_LIMIT_BREADCRUMB || key == KEY_LIMIT_TRACE_LINE || key == KEY_LIMIT_TRACE_LENGTH ||
          key == KEY_USER_PROPERTY_CACHE_LIMIT || key == KEY_DROP_OLD_REQUEST_TIME || key == KEY_SERVER_CONFIG_UPDATE_INTERVAL) {
        if (!value.is_number_unsigned()) {
          it = c.erase(it);
          continue;
        }
      } else if (key == KEY_TRACKING || key == KEY_NETWORKING || key == KEY_LOGGING || key == KEY_SESSION_TRACKING || key == KEY_VIEW_TRACKING || key == KEY_LOCATION_TRACKING || key == KEY_CUSTOM_EVENT_TRACKING || key == KEY_CONSENT_REQUIRED || key == KEY_CRASH_REPORTING) {
        if (!value.is_boolean()) {
          it = c.erase(it);
          continue;
        }
      } else if (key == KEY_EVENT_BLACKLIST || key == KEY_USER_PROPERTY_BLACKLIST || key == KEY_SEGMENTATION_BLACKLIST || key == KEY_EVENT_SEGMENTATION_BLACKLIST || key == KEY_EVENT_WHITELIST || key == KEY_USER_PROPERTY_WHITELIST || key == KEY_SEGMENTATION_WHITELIST ||
                 key == KEY_EVENT_SEGMENTATION_WHITELIST) {
        if (!value.is_array()) {
          it = c.erase(it);
          continue;
        }
      } else {
        _logger->log(LogLevel::DEBUG, "[ConfigurationModule] sanitizeConfig, removing unknown key: " + key);
        it = c.erase(it);
        continue;
      }
      ++it;
    }
  }

  void _updateConfigPeriodically(const nlohmann::json &session_params) {
    std::unique_lock<std::mutex> lock(configUpdateMutex);

    while (!stopConfigThread.load(std::memory_order_acquire)) {

      unsigned int interval = getUInt(KEY_SERVER_CONFIG_UPDATE_INTERVAL, 4);

      if (interval < 1) {
        interval = 4;
      }

      bool stopped = configUpdateCv.wait_for(lock, std::chrono::hours(interval), [&] { return stopConfigThread.load(std::memory_order_acquire); });
      if (stopped) {
        return;
      }

      lock.unlock();

      _mutex->lock();
      std::map<std::string, std::string> data = {{"method", "sc"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};
      _mutex->unlock();
      _fetchConfigFromServerHTTP(data, session_params);

      lock.lock();
    }
  }

  void _stopTimer() {
    if (_configuration->sdkBehaviorSettingsUpdatesDisabled) {
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _stopTimer, SDK behavior settings updates are disabled.");
      return;
    }

    _logger->log(LogLevel::WARNING, "[ConfigurationModule] stopTimer, stopping server config update timer thread.");
    stopConfigThread.store(true, std::memory_order_release);
    configUpdateCv.notify_all();

    if (configUpdateThread.joinable()) {
      configUpdateThread.join();
    }
  }

  void _startTimer(nlohmann::json session_params) {
    if (_configuration->sdkBehaviorSettingsUpdatesDisabled) {
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _startTimer, SDK behavior settings updates are disabled.");
      return;
    }

    if (configUpdateThread.joinable()) {
      return;
    }

    stopConfigThread.store(false, std::memory_order_release);
    configUpdateThread = std::thread(&ConfigurationModule::ConfigurationModuleImpl::_updateConfigPeriodically, this, session_params);
  }

  ~ConfigurationModuleImpl() {
    _stopTimer();
    sdk_behavior_settings.clear();

    networkingEnabled.store(true, std::memory_order_relaxed);
    trackingEnabled.store(true, std::memory_order_relaxed);
    sessionTrackingEnabled.store(true, std::memory_order_relaxed);
    viewTrackingEnabled.store(true, std::memory_order_relaxed);
    locationTrackingEnabled.store(true, std::memory_order_relaxed);
    customEventTrackingEnabled.store(true, std::memory_order_relaxed);
    crashReportingEnabled.store(true, std::memory_order_relaxed);
    eventQueueThreshold.store(0, std::memory_order_relaxed);
    requestQueueSizeLimit.store(0, std::memory_order_relaxed);
    sessionUpdateInterval.store(0, std::memory_order_relaxed);
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
  impl->_logger->log(LogLevel::DEBUG, "[ConfigurationModule] Initialized");
}

ConfigurationModule::~ConfigurationModule() { impl.reset(); }

void ConfigurationModule::fetchConfigFromServer(nlohmann::json session_params) {
  impl->_mutex->lock();
  std::map<std::string, std::string> data = {{"method", "sc"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};
  impl->_mutex->unlock();
  // Fetch SBS asynchronously
  std::thread _thread(&ConfigurationModule::ConfigurationModuleImpl::_fetchConfigFromServerHTTP, impl.get(), data, session_params);
  _thread.detach();
}

void ConfigurationModule::fetchConfigFromStorage() {
  impl->_mutex->lock();
  impl->_initializeSBSFromStorage();
  impl->_mutex->unlock();
}

void ConfigurationModule::startServerConfigUpdateTimer(nlohmann::json session_params) { impl->_startTimer(session_params); }

void ConfigurationModule::stopTimer() { impl->_stopTimer(); }

bool ConfigurationModule::isTrackingEnabled() const { return impl->trackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isNetworkingEnabled() const { return impl->networkingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isLocationTrackingEnabled() { return impl->locationTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isViewTrackingEnabled() const { return impl->viewTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isSessionTrackingEnabled() { return impl->sessionTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isCustomEventTrackingEnabled() { return impl->customEventTrackingEnabled.load(std::memory_order_acquire); }

bool ConfigurationModule::isCrashReportingEnabled() const { return impl->crashReportingEnabled.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getRequestQueueSizeLimit() const { return impl->requestQueueSizeLimit.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getEventQueueSizeLimit() { return impl->eventQueueThreshold.load(std::memory_order_acquire); }

unsigned int ConfigurationModule::getSessionUpdateInterval() { return impl->sessionUpdateInterval.load(std::memory_order_acquire); }
// namespace cly
} // namespace cly