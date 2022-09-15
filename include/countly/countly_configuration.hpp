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
   * Sets the interval for the automatic update calls
   * min value 1 (1 second), max value 600 (10 minutes)
   */
  unsigned int sessionDuration = 60;

  /**
   * Set threshold value for the number of events that can be stored locally.
   */
  unsigned int eventQueueThreshold = 100;

  /**
   * Set limit for the number of requests that can be stored locally.
   */
  unsigned int requestQueueThreshold = 1000;

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

  HTTPClientFunction http_client_function = nullptr;

  nlohmann::json metrics;
};
} // namespace cly
#endif
