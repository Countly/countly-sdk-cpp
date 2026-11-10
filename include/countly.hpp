#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include "countly/constants.hpp"
#include "countly/countly_configuration.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <deque>

#include "nlohmann/json.hpp"

#ifdef _WIN32
#undef ERROR
#endif
#include "countly/consent_module.hpp"
#include "countly/event.hpp"
#include "countly/health_check_module.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_module_base.hpp"
#include "countly/views_module.hpp"
#include <countly/configuration_module.hpp>
#include <countly/configuration_provider.hpp>
#include <countly/crash_module.hpp>
#include <countly/remote_config_store.hpp>
#include <countly/request_builder.hpp>
#include <countly/request_module.hpp>

namespace cly {
class Countly : public cly::CountlyDelegates {
public:
  Countly();

  virtual ~Countly();

  // Returns the default instance, creating it on first use.
  static Countly &getInstance();

  /**
   * Returns the instance registered under 'name', creating an uninitialized one
   * and logging a WARNING if that name is not registered yet. The empty name is
   * the default instance, so getInstance("") == getInstance().
   *
   * The returned reference is valid until that instance is destroyed by
   * destroyInstance(), destroyAllInstances() or halt().
   */
  static Countly &getInstance(const std::string &name);

  /**
   * Creates and returns the instance registered under 'name'. If that name is
   * already registered, returns the existing instance and logs a WARNING.
   */
  static Countly &createInstance(const std::string &name);

  /**
   * Returns the instance registered under 'name', or nullptr if there is none.
   * Unlike getInstance(name) this never creates anything, and unlike
   * hasInstance() followed by getInstance() it is a single lookup, so no other
   * thread can destroy the instance in between.
   */
  static Countly *findInstance(const std::string &name);

  /**
   * @return true if an instance is registered under 'name'.
   */
  static bool hasInstance(const std::string &name);

  /**
   * Destroys the instance registered under 'name'. Joins its threads, ends its
   * session and frees its database path claim. A no-op if the name is not
   * registered. Any reference or pointer to that instance dangles afterwards.
   */
  static void destroyInstance(const std::string &name);

  /**
   * Destroys every registered instance, in reverse creation order.
   */
  static void destroyAllInstances();

  /**
   * Releases process-wide networking state (libcurl's global data). Optional:
   * process exit does this automatically. Call it only after destroying every
   * instance, and only in a host that loads and unloads the SDK without exiting
   * -- a plugin host using dlopen/dlclose, for example. Logs an error and does
   * nothing if any instance is still live.
   *
   * A no-op on builds that do not use libcurl (Windows/WinHTTP,
   * COUNTLY_USE_CUSTOM_HTTP).
   */
  static void shutdownNetworking();

  // Do not implicitly generate the copy constructor, instances are not copyable.
  Countly(const Countly &) = delete;

  // Do not implicitly generate the copy assignment operator.
  void operator=(const Countly &) = delete;

  // Not movable either: modules hold `CountlyDelegates *this` and background
  // threads capture `this`, so relocating an instance is unsound.
  Countly(Countly &&) = delete;
  Countly &operator=(Countly &&) = delete;

  void alwaysUsePost(bool value);

  void setMaxRequestQueueSize(unsigned int requestQueueSize);

  void setMaxRQProcessingBatchSize(unsigned int requestQueueProcessingSize);

  void setSalt(const std::string &value);

  void setLogger(void (*fun)(LogLevel level, const std::string &message));

  void setSha256(cly::SHA256Function fun);

  void enableManualSessionControl();

  void disableAutoEventsOnUserProperties();

  void enableImmediateRequestOnStop();

  void setHTTPClient(HTTPClientFunction fun);

  void setMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version);

  /**
   * Sets the user's details, such as name and email.
   * Called before start(), the details are sent with the first session; after start(), they are sent right away.
   * Details still waiting when the device ID changes without merge are sent for the previous device ID.
   */
  void setUserDetails(const std::map<std::string, std::string> &value);

  /**
   * Sets the user's custom properties.
   * Called before start(), the properties are sent with the first session; after start(), they are sent right away.
   * Properties still waiting when the device ID changes without merge are sent for the previous device ID.
   */
  void setCustomUserDetails(const std::map<std::string, std::string> &value);

  /**
   * Multiplies the numeric value of the user's custom property 'key' by 'value' on the server.
   * Call it after start(): each call queues its own request, after any user details still waiting
   * for the first session. A call before start(), an empty key, or a value that is not a finite
   * number is ignored and logged.
   */
  void multiplyCustomUserDetail(const std::string &key, double value);

  /*
  setCountry is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setCountry(const std::string &country_code);

  /*
  setCity is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setCity(const std::string &city_name);

  /*
  setLocation is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setLocation(double lattitude, double longitude);

  /*
  Set Country code(ISO Country code), City, Location and IP address to be used for future requests.
  @param[in] countryCode: ISO Country code for the user's country
  @param[in] city: Name of the user's city.
  @param[in] gpsCoordinates: Comma separate latitude and longitude values.For example, `56.42345,123.45325`.
  @param[in] ipAddress: IpAddress like `192.168.88.33`
  */
  void setLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress);

  void setDeviceID(const std::string &value, bool same_user = false);

  void start(const std::string &app_key, const std::string &host, int port = 0, bool start_thread = false);

  void startOnCloud(const std::string &app_key);

  void stop();

  /**
   * Changes the server URL, in memory only. Host and port work as in start(): port 0 is the scheme's default and
   * trailing slashes are removed. A host that is not an absolute http:// or https:// URL is ignored and logged. Queued
   * requests go to the new server; a request already on its way finishes at the old one. The SDK then sends the consent
   * state (when consent is required), fetches the SDK behavior settings and, with enableRemoteConfig(), downloads
   * remote config. A custom HTTP client gets only the path, so it does not follow the change. Call after start().
   */
  void changeServerUrl(const std::string &host, int port = 0);

  /**
   * Changes the app key, in memory only. Running views end, and their end events, the queued events, waiting user
   * details and the end of the running session go with the old key; a session that was running then begins with the
   * new key. Queued requests keep their key (see replaceAllAppKeysInQueueWithCurrentAppKey() and
   * removeDifferentAppKeysFromQueue()). The old app's remote config values are cleared and the new app is caught up as
   * in changeServerUrl(). An empty key, "YOUR_APP_KEY" and the current key are ignored and logged. Call after start().
   */
  void changeAppKey(const std::string &appKey);

  void setUpdateInterval(size_t milliseconds);

  void addEvent(const cly::Event &event);

  /*
   * Checks and returns the size of the event queue in memory or persistent storage.
   */
  int checkEQSize();

  /*
   * Checks and returns the size of the request queue in memory or persistent storage.
   */
  int checkRQSize();

  /**
   * Clears the request queue and the event queue. Running views, timed events, user details still waiting for the
   * first session and the health check counters stay. A request already on its way is still delivered.
   */
  void flushQueues();

  /**
   * Gives every queued request the current app key, so that requests queued before changeAppKey() go to the new app.
   */
  void replaceAllAppKeysInQueueWithCurrentAppKey();

  /**
   * Removes the queued requests whose app key is not the current one, such as those queued before changeAppKey().
   */
  void removeDifferentAppKeysFromQueue();

  /**
   * Moves the queued events into a request and sends the request queue now, on the calling thread, like each update:
   * until the queue is empty, a request fails or a batch is sent. Returns at once if the SDK's own thread is sending.
   */
  void attemptToSendStoredRequests();

  /**
   * Checks and returns the size of the event queue in persistent storage.
   */
  int checkPersistentEQSize();

  /**
   * Checks and returns the size of the event queue in memory.
   */
  int checkMemoryEQSize();

  /*
   * Erases/clears the event queue in persistent storage.
   */
  void clearPersistentEQ();

  /*
   * Erases/clears the event queue in persistent storage for given ID(s) .
   */
  void removeEventWithId(const std::string &event_ids);

  /*
   * Clears the event queue (persistent or in memory depending on their availability) internally.
   */
  void clearEQInternal();

  /*
   * Attempts to update the session if the session and return true if the session was updated successfully.
   * It checks only does so if the event queue is not empty
   */
  bool attemptSessionUpdateEQ();

  /*
   * Fetches events from the persistent storage and writes them into given json array, also gives event arrays
   */
  void fillEventsIntoJson(nlohmann::json &events, std::string &event_ids);

  /*
   * Sends the given events to the request queue.
   */
  void sendEventsToRQ(const nlohmann::json &events);

  /*
   * Checks if the event queue size is over the threshold and send the events to the RQ if the threshold is reached.
   */
  void checkAndSendEventToRQ();

  void addEventToSqlite(const cly::Event &event);

  /**
   * @deprecated use setEventsToRQThreshold instead
   */
  void setMaxEvents(size_t value);

  /*
   * Sets the number of events after which all events will be sent to the RQ.
   * Minimum value is 1. Default value is 100. Maximum value is 10000.
   */
  void setEventsToRQThreshold(int value);

  void setMaxKeyLength(unsigned int value);
  void setMaxValueSize(unsigned int value);
  void setMaxSegmentationValues(unsigned int value);
  void setMaxBreadcrumbCount(unsigned int value);
  void setMaxStackTraceLinesPerThread(unsigned int value);
  void setMaxStackTraceLineLength(unsigned int value);

  void flushEvents(std::chrono::seconds timeout = std::chrono::seconds(30));

  bool beginSession();

  bool updateSession();

  bool endSession();

  void enableRemoteConfig();

  void updateRemoteConfig();

  nlohmann::json getRemoteConfigValue(const std::string &key);

  void updateRemoteConfigFor(std::string *keys, size_t key_count);

  void updateRemoteConfigExcept(std::string *keys, size_t key_count);

  static std::chrono::system_clock::time_point getTimestamp();

  std::string calculateChecksum(const std::string &salt, const std::string &data);

  /**
   * Sets the file in which the SDK keeps queued events and requests, so they survive app restarts. Call it before start().
   * Only builds with COUNTLY_USE_SQLITE use the file; without a path, or when the file cannot be opened, they keep
   * events and requests in memory instead. Builds without SQLite log a warning and ignore the path.
   */
  void setDatabasePath(const std::string &path);

  void SetPath(const std::string &path) { setDatabasePath(path); }

  void SetMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version) { setMetrics(os, os_version, device, resolution, carrier, app_version); }

  /*
   * @deprecated use setEventsToRQThreshold instead
   */
  void SetMaxEventsPerMessage(int maxEvents) { setMaxEvents(maxEvents); }

  void SetMinUpdatePeriod(int minUpdateMillis) { setUpdateInterval(minUpdateMillis); }

  void Start(const std::string &appKey, const std::string &host, int port) { start(appKey, host, port); }

  void StartOnCloud(const std::string &appKey) { startOnCloud(appKey); }

  void Stop() { stop(); }

  inline cly::ViewsModule &views() const { return *views_module.get(); }
  inline cly::CrashModule &crash() const { return *crash_module.get(); }

  void RecordEvent(const std::string &key, int count) override { addEvent(cly::Event(key, count)); }

  void RecordEvent(const std::string &key, int count, double sum) override { addEvent(cly::Event(key, count, sum)); }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count) override {
    cly::Event event(key, count);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum) override {
    cly::Event event(key, count, sum);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum, double duration) override {
    cly::Event event(key, count, sum, duration);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  void RecordLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress) override { setLocation(countryCode, city, gpsCoordinates, ipAddress); };

  /* Provide 'updateInterval' in seconds. */
  inline void setAutomaticSessionUpdateInterval(unsigned short updateInterval) {
    if (is_sdk_initialized) {
      log(LogLevel::WARNING, "[Countly]setAutomaticSessionUpdateInterval, You can not set the session duration after SDK initialization.");
      return;
    }

    configuration->sessionDuration = updateInterval;
  }

  /**
   * Disables the SDK health check. Once after start the SDK sends the server a short report about its own state:
   * how many warnings and errors it logged, the last request the server refused, how often its session, event,
   * view and crash methods were called, and which known problems occurred. It carries no data the app recorded,
   * and the server shows it in SDK Manager. With this option the report is not sent; the SDK keeps counting.
   * Call it before start().
   */
  void disableHealthCheck();

  /**
   * Disable SDK behavior settings updates that SDK performs periodically from the server.
   */
  void disableSDKBehaviorSettingsUpdates() {
    if (is_sdk_initialized) {
      log(LogLevel::WARNING, "[Countly] disableSDKBehaviorSettingsUpdates, You can not disable SDK behavior settings updates after SDK initialization.");
      return;
    }

    configuration->sdkBehaviorSettingsUpdatesDisabled = true;
  }

  /**
   * Provide SDK behavior settings in JSON format string.
   */
  void setSDKBehaviorSettings(std::string &settings_json) {
    if (is_sdk_initialized) {
      log(LogLevel::WARNING, "[Countly] setSDKBehaviorSettings, You can not provide SDK behavior settings after SDK initialization.");
      return;
    }

    if(settings_json.empty()) {
      log(LogLevel::WARNING, "[Countly] setSDKBehaviorSettings, Provided SDK behavior settings is empty.");
      return;
    }

    configuration->sdkBehaviorSettings = settings_json;
  }

  /**
   * Requires the user's consent before the SDK collects anything. Each feature then works only after its consent
   * is given, at start with setConsentEnabled() or later with giveConsent(). The SDK does not remember consent
   * across restarts: give it again at every start. While the server's SDK behavior settings contain "Consent
   * required", the server's value applies instead. Call it before start().
   */
  void setRequiresConsent(bool required);

  /**
   * Gives consent at start to the listed features (see cly::consent), and to no other feature. Has an effect only
   * while consent is required. Names this SDK does not offer are ignored with a warning. Call it before start().
   */
  void setConsentEnabled(const std::vector<std::string> &featureNames);

  /**
   * Gives consent to the listed features (see cly::consent) or feature groups. The features start at once, for
   * example a session begins with automatic session tracking, and the SDK queues the new consent state for the
   * server. Ignored while consent is not required. Call it after start().
   */
  void giveConsent(const std::vector<std::string> &featureNames);

  /**
   * Removes consent from the listed features (see cly::consent) or feature groups. The features stop at once:
   * running views and the session end, and breadcrumbs, user details waiting for the first session and the
   * location are cleared, the location also on the server. Data recorded before stays queued and is sent. The SDK
   * queues the new consent state for the server. Ignored while consent is not required. Call it after start().
   */
  void removeConsent(const std::vector<std::string> &featureNames);

  /**
   * Gives consent to every feature this SDK offers, like giveConsent().
   */
  void giveConsentAll();

  /**
   * Removes consent from every feature this SDK offers, like removeConsent().
   */
  void removeConsentAll();

  /**
   * Whether the feature, or every feature of the group, has consent. Always true while consent is not required.
   */
  bool getConsent(const std::string &featureName);

  /**
   * Gives or removes consent for the listed features or feature groups, like giveConsent() and removeConsent().
   */
  void setConsent(const std::vector<std::string> &featureNames, bool isConsentGiven);

  /**
   * Stores a named group of features, so that their consent can be given or removed at once with
   * setConsentFeatureGroup(), giveConsent() or removeConsent(). Creating a group again replaces it. A group can
   * not take the name of a feature.
   */
  void createFeatureGroup(const std::string &groupName, const std::vector<std::string> &featureNames);

  /**
   * Gives or removes consent for every feature of the group.
   */
  void setConsentFeatureGroup(const std::string &groupName, bool isConsentGiven);

  /**
   * Logs whether consent is required and whether each feature has consent. Changes nothing.
   */
  void checkAllConsent();

  /**
   * Applies a change of the server's "Consent required" setting: turning the requirement on acts like removing
   * consent from every feature that has none, lifting it like giving consent to every feature.
   */
  void ConsentRequirementChanged() override;

#ifdef COUNTLY_BUILD_TESTS
  /**
   * Convert event queue into list.
   * Warning: This method is for debugging purposes, and it is going to be removed in the future.
   * You should not be using this method.
   * @return a vector object containing events.
   */
  std::vector<std::string> debugReturnStateOfEQ();

  /**
   * Injects a raw (possibly malformed) string directly into the in-memory event
   * queue, bypassing serialization. Used only by the mutex exception-safety test
   * to force a parse failure inside the locked section of updateSession().
   * Warning: This method is for debugging purposes, and it is going to be removed in the future.
   */
  void debugInjectRawEvent(const std::string &raw);

  /**
   * This function should not be used as it will be removed in a future release.
   * It is currently added as a temporary workaround.
   */
  inline std::function<void(LogLevel, const std::string &)> getLogger() { return logger->getLogger(); }

  /**
   * This function should not be used as it will be removed in a future release.
   * It is currently added as a temporary workaround.
   */
  inline void processRQDebug() {
    if (is_sdk_initialized) {
      requestModule->processQueue(mutex);
    }
  }

  inline void clearRequestQueue() {
    if (is_sdk_initialized) {
      // serialize storage access with the background processQueue thread
      std::lock_guard<std::mutex> lk(*mutex);
      requestModule->clearRequestQueue();
    }
  }

  inline const CountlyConfiguration &getConfiguration() { return *configuration.get(); }

  /**
   * The health check counters of this instance.
   * Warning: This method is for debugging purposes, and it is going to be removed in the future.
   */
  inline std::shared_ptr<cly::HealthCheckModule> debugHealthCheck() { return health_check; }

  static void halt();

  static size_t debugClaimedPathCount();

  static int debugLiveInstanceCount();
#endif

private:
  /**
   * Copies the default instance's logger callback onto a freshly created
   * instance. Without this, a WARNING logged on a brand-new instance is
   * swallowed: LoggerModule::log() no-ops when no callback is set.
   */
  static void inheritDefaultLogger(Countly &instance);

  /**
   * Gives back this instance's database path claim. A no-op when no claim is
   * held, and compiled to nothing on non-SQLite builds.
   */
  void releaseDatabasePathClaim();

  /**
   * Starts a remote-config fetch on a detached thread. Never blocks: neither the
   * caller (a UI thread must not stall on an HTTP timeout) nor destruction. At
   * most one fetch is in flight per instance; a call made while one is running is
   * logged and dropped.
   *
   * The thread body captures no reference to this object -- only shared_ptrs to
   * the modules and the value store it needs -- so it stays valid however long it
   * outlives the instance.
   *
   * @param data: request parameters, copied into the thread
   * @param merge: true merges the response into the stored values, false replaces
   *               them wholesale
   * @param caller: public method name, used in log messages
   * @return true if a fetch was started
   */
  bool startRemoteConfigFetch(const std::map<std::string, std::string> &data, bool merge, const char *caller);

  void _deleteThread();
  void _sendIndependantLocationRequest();
  void log(LogLevel level, const std::string &message);
  bool createEventTableSchema();

  void _changeDeviceIdWithMerge(const std::string &value);

  void _changeDeviceIdWithoutMerge(const std::string &value);

  /**
   * Queues a request with the stored user details for the current device ID and drops the stored copy,
   * so a later session does not send them again. The caller must hold the instance mutex.
   */
  void queueUserDetails();

  /**
   * Queues a request with the given user details for the current device ID. The caller must hold the instance mutex.
   */
  void queueUserDetails(const nlohmann::json &userDetails);

  std::chrono::steady_clock::duration getSessionDuration(std::chrono::steady_clock::time_point now);

  std::chrono::steady_clock::duration getSessionDuration();

  void updateLoop();
  void packEvents();

  /**
   * Begins a session, counted in the health check as a call of the app or, with 'automatic', of the SDK.
   * A call of the app while a session runs is reported as log code w760.
   */
  bool beginSessionInternal(bool automatic);

  /**
   * Updates the session, counted in the health check as a call of the app or, with 'automatic', of the SDK.
   * A call of the app in manual session control without a running session is reported as log code w760.
   * Without the "sessions" consent a call of the app is ignored, while the SDK's own call still moves the queued
   * events into a request.
   */
  bool updateSessionInternal(bool automatic);

  /**
   * Ends the session, counted in the health check as a call of the app or, with 'automatic', of the SDK.
   * A call of the app without a running session is reported as log code w760.
   */
  bool endSessionInternal(bool automatic);

  /**
   * Whether the public call 'caller' may run: never inside the log callback, and not before start(), which is logged
   * with w101.
   */
  bool isCallAllowed(const char *caller);

  /**
   * Queues the end_session request of the running session and marks the session ended, counted in the health check as
   * a call of the SDK with 'automatic'. The caller holds the instance mutex.
   */
  void queueEndSession(bool automatic);

  /**
   * After a server URL or app key change, fetches the SDK behavior settings and, with enableRemoteConfig(), remote
   * config, unless 'remoteConfigRequested' says a session start downloads it already. The caller does not hold the
   * instance mutex.
   */
  void fetchAfterServerChange(bool remoteConfigRequested);

  /**
   * Sends the health check once, as an immediate request to /i, unless it is disabled, the SDK behavior settings
   * turn networking off, or it was sent already. It carries the base request parameters, the app version as
   * metrics and the counters. Only what it reported is taken away from the counters, and only once the server
   * accepted it with a 2xx answer that is a JSON object with a "result". Runs on the update thread, so the
   * request never holds up start() and finishes before the instance is destroyed.
   */
  void sendHealthCheck();

  /**
   * Stores the health check counters when a session update interval passed since they were last stored.
   */
  void saveHealthCheckOnTimer();

  /**
   * Where a consent change comes from: a call of the app, the silent removal of a device ID change without merge,
   * or the server's "Consent required" setting.
   */
  enum class ConsentChangeSource { App, DeviceIdChangedWithoutMerge, ServerRequirement };

  /**
   * Whether the feature has consent; always true while consent is not required.
   */
  bool hasConsent(const char *feature) const;

  /**
   * Whether an event with this key may be recorded: a custom event needs the "events" consent, an internal event
   * the consent of its own feature, and a "[CLY]_" key that is no known internal event the "events" consent.
   */
  bool hasEventConsent(const std::string &key) const;

  /**
   * Whether location is on: the "location" consent is given and the SDK behavior settings allow location tracking.
   * While it is off, a session start carries the empty location, which keeps the server from working out the
   * location from the IP address.
   */
  bool isLocationOn();

  /**
   * The shared part of the public calls that give or remove consent: checks the call, expands groups, records w402
   * for unknown names and runs the change for the features whose consent changes.
   */
  void changeConsent(const std::vector<std::string> &names, bool given, const char *caller);

  /**
   * Changes the consent of the features: the reactions that need the old consent run first, then the flags change,
   * the consent state is queued for the server, and the reactions to the new state run. The silent removal of a
   * device ID change without merge queues no consent state and no location erase. The caller holds
   * consent_change_mutex and not the instance mutex.
   */
  void runConsentChange(const std::vector<std::string> &features, bool given, ConsentChangeSource source);

  /**
   * The reactions that run while the features still have consent, before it is removed: the running views and the
   * session end.
   */
  void consentWillBeRemoved(const std::vector<std::string> &features);

  /**
   * The reactions to the new consent of the features. Removed: the breadcrumbs, the user details waiting for the
   * first session and the stored location are cleared, and a location erase is queued unless the removal is silent.
   * Given: with automatic sessions a session begins, and the automatic remote config download runs.
   */
  void consentChanged(const std::vector<std::string> &features, bool given, ConsentChangeSource source);

  /**
   * Queues the consent state of every feature this SDK offers.
   */
  void queueConsentState();

  /**
   * Clears the stored location values.
   */
  void clearStoredLocation();

  /**
   * Applies the server's stored "Consent required" setting, and while consent is required, queues the consent
   * state and, when location is off and no session start will be sent, a location erase. Called at the end of start().
   */
  void activateConsent();
  bool began_session = false;
  std::atomic<bool> is_being_disposed{false};
  std::atomic<bool> is_sdk_initialized{false};

  // steady_clock, so a system clock change can't distort the session duration
  std::chrono::steady_clock::time_point last_sent_session_request;
  nlohmann::json session_params;

  std::unique_ptr<std::thread> thread;

  // Remote config values, and the in-flight flag for the fetch that writes them.
  //
  // Held behind a shared_ptr because the fetch thread is detached: destruction
  // must not wait for an HTTP round trip, so the thread can outlive this object
  // and therefore must not touch it. Everything the fetch needs is reached
  // through shared_ptrs it holds itself, this store included.
  std::shared_ptr<cly::RemoteConfigStore> remote_config_store = std::make_shared<cly::RemoteConfigStore>();

  std::unique_ptr<cly::CrashModule> crash_module;
  std::unique_ptr<cly::ViewsModule> views_module;

  // Health check counters; created with the instance, so warnings logged before start() are counted too.
  std::shared_ptr<cly::HealthCheckModule> health_check = std::make_shared<cly::HealthCheckModule>();

  // Consent state; created with the instance, so the consent options can be set before start().
  std::shared_ptr<cly::ConsentModule> consent_module = std::make_shared<cly::ConsentModule>();
  // Serializes consent changes, their reactions and device ID changes without merge. Taken before the instance mutex.
  // The destructor takes it once, so that no server requirement change runs while it tears the modules down.
  std::mutex consent_change_mutex;
  // Set at the end of start(); before that, a change of the server's requirement only updates the state.
  bool consent_active = false;
  std::atomic<bool> health_check_sent{false};
  std::chrono::steady_clock::time_point last_health_check_save = std::chrono::steady_clock::now();

  std::shared_ptr<cly::CountlyConfiguration> configuration;
  std::shared_ptr<cly::LoggerModule> logger;
  std::shared_ptr<cly::RequestBuilder> requestBuilder;
  std::shared_ptr<cly::RequestModule> requestModule;
  std::shared_ptr<cly::StorageModuleBase> storageModule;
  std::shared_ptr<cly::ConfigurationModule> configurationModule;
  std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();

  bool is_queue_being_processed = false;
  std::atomic<bool> enable_automatic_session{false};
  std::atomic<bool> stop_thread{false};
  std::atomic<bool> running{false};
  std::condition_variable stop_cv; // Wakes updateLoop immediately on stop
  size_t wait_milliseconds = COUNTLY_KEEPALIVE_INTERVAL;

  size_t max_events = COUNTLY_MAX_EVENTS_DEFAULT;
  // Members are the same in every build, so code compiled without COUNTLY_USE_SQLITE still matches the library
  std::deque<std::string> event_queue;
  std::string database_path;
  // The normalized database path this instance has claimed in the process-wide
  // registry, empty when it holds no claim.
  std::string claimed_database_path;
  // Chosen in start(): true when this instance keeps its queues in the SQLite database
  bool use_sqlite_storage = false;

  bool remote_config_enabled = false;
};
} // namespace cly
#endif
