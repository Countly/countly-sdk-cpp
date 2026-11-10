#ifndef REQUEST_MODULE_HPP_
#define REQUEST_MODULE_HPP_
#include <functional>
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
   * Sends a request that does not go through the request queue, such as a remote config download or the health
   * check, with the number of queued requests as "rr".
   *
   * @param path: endpoint path, for example "/o/sdk"
   * @param data: the request built with the base request parameters
   * @return the server's answer
   */
  HTTPResponse sendImmediate(const std::string &path, const std::string &data);

  /**
   * SDK central execution call for processing requests in the request queue.
   * Only one sender is active at a time. Requests are processed in order.
   */
  void processQueue(std::shared_ptr<std::mutex> mutex);

  void addRequestToQueue(const std::map<std::string, std::string> &data);

  /**
   * Clears the request queue. A request that processQueue() is sending meanwhile is not removed again once it is
   * delivered. The caller holds the mutex it passes to processQueue().
   */
  void clearRequestQueue();

  /**
   * Replaces the server URL and the port that the following requests go to, the way start() sets them: a URL without
   * a scheme gets "http://" in front, trailing slashes are removed, and a port of 0 becomes the scheme's default port.
   */
  void setServerUrl(const std::string &url, unsigned int port);

  /**
   * Gives every queued request the app key. The caller holds the mutex it passes to processQueue().
   */
  void replaceAppKeysInQueue(const std::string &appKey);

  /**
   * Removes the queued requests whose app key is not the given one. The caller holds the mutex it passes to
   * processQueue().
   * @return the number of removed requests
   */
  long long removeOtherAppKeysFromQueue(const std::string &appKey);

  long long RQSize();
  void setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider); // try injecting

  /**
   * Sets what runs after a queued merge request (one with "old_device_id") was delivered, on the thread that sent
   * it and without the instance mutex held.
   */
  void setOnMergeDelivered(std::function<void()> callback);

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
