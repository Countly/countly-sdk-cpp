#ifndef COUNTLY_CONFIGURATION_HPP_
#define COUNTLY_CONFIGURATION_HPP_
#include <string>

namespace cly {

struct CountlyConfiguration {
public:
  /// <summary>
  /// URL of the Countly server to submit data to.
  /// Mandatory field.
  /// </summary>
  std::string serverUrl;

  /// <summary>
  /// App key for the application being tracked.
  /// Mandatory field.
  /// </summary>
  std::string appKey = null;

  /// <summary>
  /// Unique ID for the device the app is running on.
  /// </summary>
  std::string deviceId = null;

  /// <summary>
  /// Set to prevent parameter tampering.
  /// </summary>
  std::string salt = null;

  CountlyConfiguration();
  ~CountlyConfiguration();
};
} // namespace cly
#endif
