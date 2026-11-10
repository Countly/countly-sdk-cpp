#ifndef COUNTLY_CONFIGURATION_HPP_
#define COUNTLY_CONFIGURATION_HPP_
#include "countly/constants.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cly {
/**
 * Where the device ID came from. Sent with every request as the "t" parameter.
 */
enum class DeviceIdType { DEVELOPER_SUPPLIED = 0, SDK_GENERATED = 1 };

/**
 * A crash report as the crash filter sees it. The filter may change any field before the report is queued.
 */
struct CrashData {
  /** The crash title, sent as "_name". */
  std::string description;
  /** The stack trace, sent as "_error". */
  std::string stackTrace;
  /** The custom segmentation, sent as "_custom". */
  std::map<std::string, std::string> crashSegmentation;
  /** The breadcrumbs, oldest first, sent joined as "_logs". */
  std::vector<std::string> breadcrumbs;
  /** The device metrics, such as "_os" and "_app_version". */
  std::map<std::string, std::string> crashMetrics;
  /** Whether the crash ended the app; sent as the opposite, "_nonfatal". */
  bool fatal = false;
};

/**
 * Sees every crash report before it is queued and may change it. Returns true to drop the report.
 */
using CrashFilterCallback = std::function<bool(CrashData &crash)>;

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
   * Where the device ID came from.
   */
  DeviceIdType deviceIdType = DeviceIdType::DEVELOPER_SUPPLIED;

  /**
   * Set to prevent parameter tampering.
   */
  std::string salt;

  /**
   * Path to the database. Only builds with COUNTLY_USE_SQLITE use it.
   */
  std::string databasePath;

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
   * Seconds to wait after a failed request before it is tried again.
   */
  unsigned int failedRequestCooldownSeconds = 60;

  /**
   * Hours after which a stored request is dropped without being sent, 0 to keep requests however old they are.
   */
  unsigned int requestDropAgeHours = 0;

  /**
   * Whether sending pauses for a while after the server answered slowly.
   */
  bool backoffMechanismEnabled = true;

  /**
   * Whether a request queue far over its limit is trimmed by at most 100 requests at a time.
   */
  bool gradualRequestCleanerEnabled = true;

  /**
   * Whether remote config downloads also enroll the user in the A/B tests of the keys they return.
   */
  bool enrollABOnRCDownload = false;

  /**
   * Whether start() replaces a device ID stored by an earlier start with the given or a newly generated one.
   */
  bool clearStoredDeviceID = false;

  /**
   * Segmentation added to every crash report.
   */
  std::map<std::string, std::string> crashSegmentation;

  /**
   * Sees every crash report before it is queued, null when the app set none.
   */
  CrashFilterCallback crashFilter = nullptr;

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

  /**
   * Whether the SDK sends its health check after start.
   */
  bool healthCheckEnabled = true;

  std::string sdkBehaviorSettings;

  CountlyConfiguration(const std::string appKey, std::string serverUrl) {
    this->appKey = appKey;
    this->serverUrl = serverUrl;
  }
};
} // namespace cly
#endif
