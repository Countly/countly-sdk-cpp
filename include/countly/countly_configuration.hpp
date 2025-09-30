#ifndef COUNTLY_CONFIGURATION_HPP_
#define COUNTLY_CONFIGURATION_HPP_
#include "countly/constants.hpp"
#include <string>

namespace cly {
struct CountlyConfiguration {
  /**
   * URL of the Countly server to submit data to.
   * Mandatory field.
   */
  std::string serverUrl;

  /**
   * App key for the application being tracked.
   * Mandatory field.
   */
  std::string appKey;

  /**
   * Unique ID for the device the app is running on.
   */
  std::string deviceId;

  /**
   * Set to prevent parameter tampering.
   */
  std::string salt;

  /**
   * Path to the database.
   */
#ifdef COUNTLY_USE_SQLITE
  std::string databasePath;
#endif

  /**
   * Sets the interval for the automatic update calls
   */
  unsigned int sessionDuration = 60;

  /**
   * Set threshold value for the number of events that can be stored locally.
   */
  int eventQueueThreshold = 100;

  /**
   * Set limit for the number of requests that can be stored locally.
   */
  unsigned int requestQueueThreshold = 1000;

  /**
   * Set limit for the number of requests that can be processed at a time.
   */
  unsigned int maxProcessingBatchSize = 100;

  /**
   * Set the maximum amount of breadcrumbs.
   */
  unsigned int breadcrumbsThreshold = 100;

  /**
   * Set to send all requests made to the Countly server using HTTP POST.
   */
  bool forcePost = false;

  unsigned int port = 443;

  SHA256Function sha256_function = nullptr;

  bool manualSessionControl = false;

  HTTPClientFunction http_client_function = nullptr;

  nlohmann::json metrics;

  CountlyConfiguration(const std::string appKey, std::string serverUrl) {
    this->appKey = appKey;
    this->serverUrl = serverUrl;
  }
};
} // namespace cly
#endif
