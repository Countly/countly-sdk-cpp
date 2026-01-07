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
static constexpr const char *KEY_LOGGING = "log";
static constexpr const char *KEY_SESSION_UPDATE_INTERVAL = "sui";
static constexpr const char *KEY_SESSION_TRACKING = "st";
static constexpr const char *KEY_VIEW_TRACKING = "vt";
static constexpr const char *KEY_LOCATION_TRACKING = "lt";
static constexpr const char *KEY_CUSTOM_EVENT_TRACKING = "cet";
static constexpr const char *KEY_CRASH_REPORTING = "crt";
static constexpr const char *KEY_SERVER_CONFIG_UPDATE_INTERVAL = "scui";

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
public:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestBuilder> _requestBuilder;
  std::shared_ptr<StorageModuleBase> _storageModule;
  std::shared_ptr<RequestModule> _requestModule;
  std::shared_ptr<std::mutex> _mutex;
  std::shared_ptr<std::mutex> sdk_behavior_settings_mutex = std::make_shared<std::mutex>();
  nlohmann::json sdk_behavior_settings;
  ConfigurationModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex)
      : _configuration(config), _logger(logger), _requestBuilder(requestBuilder), _storageModule(storageModule), _requestModule(requestModule), _mutex(mutex) {}

  ~ConfigurationModuleImpl() { _logger.reset(); }

  void _fetchConfigFromServerHTTP(const std::map<std::string, std::string> &data) {
    HTTPResponse response = _requestModule->sendHTTP("/o/sdk", _requestBuilder->serializeData(data));
    if (response.success && response.data.is_object() && response.data.contains(KEY_CONFIG)) {
      sdk_behavior_settings_mutex->lock(); // why lock here? we already got the new one so lets await other accessors to get the new one
      sanitizeConfig(response.data[KEY_CONFIG]);
      sdk_behavior_settings = response.data[KEY_CONFIG];
      _logger->log(LogLevel::INFO, "[ConfigurationModule] _fetchConfigFromServerHTTP, SDK config:\n" + sdk_behavior_settings.dump(2));
      sdk_behavior_settings_mutex->unlock();
    } else {
      _logger->log(LogLevel::WARNING, cly::utils::format_string("[ConfigurationModule] _fetchConfigFromServerHTTP, failed to fetch response_success: [%s]", response.success ? "true" : "false"));
    }
  }

  bool getBool(const char *key, bool defaultValue) const {
    sdk_behavior_settings_mutex->lock();
    if (!sdk_behavior_settings.is_object()) {
      sdk_behavior_settings_mutex->unlock();
      return defaultValue;
    }

    auto it = sdk_behavior_settings.find(key);
    if (it == sdk_behavior_settings.end() || !it->is_boolean()) {
      sdk_behavior_settings_mutex->unlock();
      return defaultValue;
    }

    bool value = it->get<bool>();
    sdk_behavior_settings_mutex->unlock();
    return value;
  }

  unsigned int getUInt(const char *key, unsigned int defaultValue) const {
    sdk_behavior_settings_mutex->lock();
    if (!sdk_behavior_settings.is_object()) {
      sdk_behavior_settings_mutex->unlock();
      return defaultValue;
    }

    auto it = sdk_behavior_settings.find(key);
    if (it == sdk_behavior_settings.end() || !it->is_number_unsigned()) {
      sdk_behavior_settings_mutex->unlock();
      return defaultValue;
    }

    unsigned int value = it->get<unsigned int>();
    sdk_behavior_settings_mutex->unlock();
    return value;
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
};

ConfigurationModule::ConfigurationModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule,
                                         std::shared_ptr<std::mutex> mutex) {
  impl.reset(new ConfigurationModuleImpl(config, logger, requestBuilder, storageModule, requestModule, mutex));
  impl->_logger->log(LogLevel::DEBUG, "[ConfigurationModule] Initialized");
}

ConfigurationModule::~ConfigurationModule() { impl.reset(); }

void ConfigurationModule::fetchConfigFromServer(nlohmann::json session_params) {
  impl->_mutex->lock();
  std::map<std::string, std::string> data = {{"method", "sc"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};
  impl->_mutex->unlock();
  // Fetch SBS asynchronously
  std::thread _thread(&ConfigurationModule::ConfigurationModuleImpl::_fetchConfigFromServerHTTP, impl.get(), data);
  _thread.detach();
}

bool ConfigurationModule::isTrackingEnabled() const { return impl->getBool(KEY_TRACKING, true); }

bool ConfigurationModule::isNetworkingEnabled() const { return impl->getBool(KEY_NETWORKING, true); }

bool ConfigurationModule::isLoggingEnabled() const { return impl->getBool(KEY_LOGGING, false); }

bool ConfigurationModule::isLocationTrackingEnabled() { return impl->getBool(KEY_LOCATION_TRACKING, true); }

bool ConfigurationModule::isViewTrackingEnabled() { return impl->getBool(KEY_VIEW_TRACKING, true); }

bool ConfigurationModule::isSessionTrackingEnabled() { return impl->getBool(KEY_SESSION_TRACKING, true); }

bool ConfigurationModule::isCustomEventTrackingEnabled() { return impl->getBool(KEY_CUSTOM_EVENT_TRACKING, true); }

bool ConfigurationModule::isCrashReportingEnabled() { return impl->getBool(KEY_CRASH_REPORTING, true); }

unsigned int ConfigurationModule::getRequestQueueSizeLimit() const { return impl->getUInt(KEY_REQ_QUEUE_SIZE, impl->_configuration->requestQueueThreshold); }

unsigned int ConfigurationModule::getEventQueueSizeLimit() { return impl->getUInt(KEY_EVENT_QUEUE_SIZE, impl->_configuration->eventQueueThreshold); }

unsigned int ConfigurationModule::getSessionUpdateInterval() { return impl->getUInt(KEY_SESSION_UPDATE_INTERVAL, impl->_configuration->sessionDuration); }
// namespace cly
} // namespace cly