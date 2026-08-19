#ifndef REQUEST_MODULE_HPP_
#define REQUEST_MODULE_HPP_
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/request_builder.hpp"
#include "countly/storage_module_base.hpp"
#include "countly/configuration_provider.hpp"

namespace cly {
class RequestModule {

public:
  ~RequestModule();
  RequestModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule);

  HTTPResponse sendHTTP(std::string path, std::string data);

  /**
   * SDK central execution call for processing requests in the request queue.
   * Only one sender is active at a time. Requests are processed in order.
   */
  void processQueue(std::shared_ptr<std::mutex> mutex);

  void addRequestToQueue(const std::map<std::string, std::string> &data);

  /**
   * Clear request queue.
   * Warning: This method is for debugging purposes, and it is going to be removed in the future.
   * You should not be using this method.
   * @return a vector object containing requests.
   */
  void clearRequestQueue();

  long long RQSize();
  void setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider); // try injecting

  /**
   * Process-wide network initialisation. Idempotent and harmless to call at any
   * time; a no-op unless the SDK is built against libcurl.
   */
  static void initGlobalNetworking();

#ifdef COUNTLY_BUILD_TESTS
  /** Times curl_global_init actually ran. Structurally at most one. */
  static int globalNetworkingInitCount();
  /** Times initGlobalNetworking() was called, i.e. how much was deduplicated. */
  static int globalNetworkingInitRequests();
  static bool globalNetworkingReleased();
#endif

private:
  // Tearing networking down is only safe once no instance is live, and only
  // Countly::shutdownNetworking() knows that. Keeping this private stops an
  // integrator reaching past the check.
  friend class Countly;

  /**
   * Process-wide network teardown. Idempotent, and a no-op unless the SDK is
   * built against libcurl.
   */
  static void releaseGlobalNetworking();

  class RequestModuleImpl;
  std::unique_ptr<RequestModuleImpl> impl;
  std::weak_ptr<ConfigurationProvider> _configProvider;
};
} // namespace cly
#endif
