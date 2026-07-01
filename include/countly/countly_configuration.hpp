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
   * SDK internal limits (defaults; overridable at runtime by SDK Behavior Settings).
   */
  unsigned int maxKeyLength = COUNTLY_MAX_KEY_LENGTH_DEFAULT;
  unsigned int maxValueSize = COUNTLY_MAX_VALUE_SIZE_DEFAULT;
  unsigned int maxSegmentationValues = COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT;
  unsigned int maxStackTraceLinesPerThread = COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT;
  unsigned int maxStackTraceLineLength = COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT;

  /**
   * Set to send all requests made to the Countly server using HTTP POST.
   */
  bool forcePost = false;

  unsigned int port = 443;

  SHA256Function sha256_function = nullptr;

  bool manualSessionControl = false;

  bool autoEventsOnUserProperties = true;

  /**
   * Enable immediate stop notification using a condition variable.
   * When enabled, the update loop wakes immediately on stop instead of
   * waiting for the current sleep interval to expire.
   */
  bool immediateRequestOnStop = false;

  HTTPClientFunction http_client_function = nullptr;

  nlohmann::json metrics;

  bool sdkBehaviorSettingsUpdatesDisabled = false;

  std::string sdkBehaviorSettings;

  CountlyConfiguration(const std::string appKey, std::string serverUrl) {
    this->appKey = appKey;
    this->serverUrl = serverUrl;
  }
};
} // namespace cly
#endif
