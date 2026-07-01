#ifndef CONFIGURATION_MODULE_HPP_
#define CONFIGURATION_MODULE_HPP_

#include "countly/configuration_provider.hpp"
#include "countly/constants.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/request_builder.hpp"
#include "countly/request_module.hpp"
#include "countly/storage_module_base.hpp"

#include <set>
#include <map>

namespace cly {

template<typename T>
struct FilterList {
  T filterList;
  bool isWhitelist = false;
};

class ConfigurationModule : public ConfigurationProvider {

public:
  ~ConfigurationModule();
  ConfigurationModule(cly::CountlyDelegates *cly, std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule,
                      std::shared_ptr<std::mutex> mutex);

  void fetchConfigFromServer(nlohmann::json session_params);
  void fetchConfigFromStorage();
  void startServerConfigUpdateTimer(nlohmann::json session_params);
  void stopTimer();
  bool isTrackingEnabled() const override;
  bool isNetworkingEnabled() const override;

  bool isLocationTrackingEnabled();
  bool isViewTrackingEnabled() const override;
  bool isSessionTrackingEnabled();
  bool isCustomEventTrackingEnabled();
  bool isCrashReportingEnabled() const override;

  unsigned int getRequestQueueSizeLimit() const override;
  SDKLimits getLimits() const override;
  unsigned int getEventQueueSizeLimit();
  unsigned int getSessionUpdateInterval();

  FilterList<std::set<std::string>> getEventFilterList() const;
  FilterList<std::set<std::string>> getUserPropertyFilterList() const;
  FilterList<std::set<std::string>> getSegmentationFilterList() const;
  FilterList<std::map<std::string, std::set<std::string>>> getEventSegmentationFilterList() const;

private:
  class ConfigurationModuleImpl;
  std::unique_ptr<ConfigurationModuleImpl> impl;
};
} // namespace cly
#endif
