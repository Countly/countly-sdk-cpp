#ifndef COUNTLY_CONFIGURATION_HPP_
#define COUNTLY_CONFIGURATION_HPP_
#include <string>
#include "countly/constants.hpp"

namespace cly {
struct CountlyConfiguration {
  /// <summary>
  /// URL of the Countly server to submit data to.
  /// Mandatory field.
  /// </summary>
  std::string serverUrl;

  /// <summary>
  /// App key for the application being tracked.
  /// Mandatory field.
  /// </summary>
  std::string appKey;

  /// <summary>
  /// Unique ID for the device the app is running on.
  /// </summary>
  std::string deviceId;

  /// <summary>
  /// Set to prevent parameter tampering.
  /// </summary>
  std::string salt;

  /// <summary>
  /// Sets the interval for the automatic update calls
  /// min value 1 (1 second), max value 600 (10 minutes)
  /// </summary>
  unsigned int sessionDuration = 60;

  /// <summary>
  /// Set threshold value for the number of events that can be stored locally.
  /// </summary>
  unsigned int eventQueueThreshold = 100;

  /// <summary>
  /// Set limit for the number of requests that can be stored locally.
  /// </summary>
  unsigned int requestQueueThreshold = 1000;

  /// <summary>
  /// Set the maximum amount of breadcrumbs.
  /// </summary>
  unsigned int breadcrumbsThreshold = 100;

  /// <summary>
  /// Set to send all requests made to the Countly server using HTTP POST.
  /// </summary>
  bool enablePost = false;

  unsigned int port = 443;

  SHA256Function sha256_function = nullptr;

  HTTPClientFunction http_client_function = nullptr;
};
} // namespace cly
#endif
