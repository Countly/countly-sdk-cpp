#include "countly/internal_limits.hpp"
#include "countly/path_utils.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef COUNTLY_USE_CUSTOM_SHA256
#include "openssl/sha.h"
#endif

#include "countly.hpp"

#ifdef COUNTLY_USE_SQLITE
#include "countly/sqlite_utils.hpp"
#include "sqlite3.h"
#endif

namespace cly {

namespace {
/**
 * Number of live Countly objects in this process, registry-held and
 * integrator-owned alike. shutdownNetworking() refuses to tear down networking
 * while this is non-zero.
 */
std::atomic<int> live_instance_count(0);

/**
 * Serialises instance construction and destruction against
 * shutdownNetworking(), so that no instance can come into existence between
 * that function reading the count and releasing networking.
 *
 * Lock order: this is acquired *inside* the registry's instance mutex, because
 * Countly() is constructed while the registry holds it. shutdownNetworking()
 * must therefore finish with the registry before acquiring this.
 */
std::mutex networking_lifecycle_mutex;

/**
 * Number of remote-config fetches running on detached threads across the whole
 * process. shutdownNetworking() refuses while this is non-zero: such a thread can
 * outlive the instance that started it, and releasing libcurl underneath one
 * would crash it.
 */
std::atomic<int> inflight_remote_config_fetches(0);

/**
 * Everything a remote-config fetch needs, each piece owned by the fetch itself.
 *
 * The fetch runs detached, so it may still be in an HTTP call after the Countly
 * instance that started it is destroyed. Holding shared_ptrs -- and no pointer to
 * the instance -- is what makes that safe: nothing the body touches can be freed
 * while the body runs.
 *
 * Note which module is absent. ConfigurationModule keeps a raw CountlyDelegates
 * pointer and calls through it from its SBS timer, so a reference held here would
 * postpone its destruction past ~Countly and leave that timer running against a
 * destroyed instance. Its one contribution, the networking-enabled check, is made
 * before the thread starts instead.
 */
struct RemoteConfigFetch {
  std::shared_ptr<RequestModule> requestModule;
  std::shared_ptr<RequestBuilder> requestBuilder;
  std::shared_ptr<LoggerModule> logger;
  std::shared_ptr<RemoteConfigStore> store;
  std::map<std::string, std::string> data;
  // true merges the response into the stored values, false replaces them.
  bool merge = false;
};

/**
 * Body of a detached remote-config fetch. A free function rather than a Countly
 * member precisely so it cannot reach the instance.
 */
void runRemoteConfigFetch(std::shared_ptr<RemoteConfigFetch> fetch) {
  // Clear the in-flight flag and the process counter however this exits.
  struct Guard {
    std::shared_ptr<RemoteConfigFetch> fetch;
    ~Guard() {
      fetch->store->fetch_running.store(false);
      inflight_remote_config_fetches.fetch_sub(1);
    }
  } guard{fetch};

  const HTTPResponse response = fetch->requestModule->sendHTTP("/o/sdk", fetch->requestBuilder->serializeData(fetch->data));
  if (!response.success) {
    return;
  }

  std::lock_guard<std::mutex> lk(fetch->store->mutex);
  if (fetch->merge) {
    for (auto it = response.data.begin(); it != response.data.end(); ++it) {
      fetch->store->values[it.key()] = it.value();
    }
  } else {
    fetch->store->values = response.data;
  }
}
} // namespace

Countly::Countly() {
  {
    std::lock_guard<std::mutex> lk(networking_lifecycle_mutex);
    live_instance_count.fetch_add(1);
  }
  crash_module = nullptr;
  views_module = nullptr;
  logger.reset(new cly::LoggerModule());
  configuration.reset(new cly::CountlyConfiguration("", ""));
}

Countly::~Countly() {
  is_being_disposed = true;
  // Deliberately does not wait for an in-flight remote-config fetch: that would
  // make destruction block for an HTTP round trip. The fetch runs detached and
  // holds its own references to everything it touches, so it stays valid on its
  // own. The SBS timer is joined by configurationModule.reset() below.
  stop();
  releaseDatabasePathClaim();
  crash_module.reset();
  views_module.reset();
  configurationModule.reset();
  logger.reset();
  {
    std::lock_guard<std::mutex> lk(networking_lifecycle_mutex);
    live_instance_count.fetch_sub(1);
  }
}

namespace {

const char *const DEFAULT_INSTANCE_NAME = "";

/**
 * Lock-free fast path for the default instance.
 *
 * getInstance() is the hottest entry point in the SDK -- the sample and the Qt
 * demo call it at every call site rather than caching the reference -- and it
 * used to be a null check and a dereference. Routing it through the registry
 * would have cost a mutex, a linear scan and two atomic refcount operations on
 * every call, which is a tax on integrators who never asked for a second
 * instance. This pointer is published under the registry lock when the default
 * instance is created and cleared when it is destroyed.
 */
std::atomic<Countly *> default_instance_cache(nullptr);

/**
 * Process-wide state shared by every Countly instance:
 *   - the named-instance table, including the unnamed default instance
 *   - the set of claimed SQLite database paths
 *
 * Reached only through registry(), so initialisation is thread-safe by way of
 * C++11 function-local statics.
 *
 * The constructor calls RequestModule::initGlobalNetworking() so libcurl's
 * global guard finishes construction *before* this registry and is therefore
 * destroyed *after* it. That matters at process exit: the registry destroys its
 * remaining instances, and ~Countly still needs networking for endSession().
 */
class InstanceRegistry {
public:
  InstanceRegistry() { RequestModule::initGlobalNetworking(); }

  ~InstanceRegistry() { clear(); }

  /**
   * Returns the instance registered under 'name', creating it if absent.
   * *created_out reports whether this call created it.
   */
  /**
   * Returns a raw pointer rather than a shared_ptr on purpose. Handing ownership
   * out would mean destroyInstance() could return while another thread still
   * held a reference, leaving the instance -- and its database path claim --
   * alive after the call that was supposed to free it. The registry is the sole
   * owner; the returned pointer is valid until that entry is destroyed, which is
   * exactly the lifetime the public API documents.
   */
  Countly *getOrCreate(const std::string &name, bool *created_out) {
    std::lock_guard<std::mutex> lk(_instances_mutex);
    for (size_t index = 0; index < _instances.size(); index++) {
      if (_instances[index].first == name) {
        if (created_out != nullptr) {
          *created_out = false;
        }
        return _instances[index].second.get();
      }
    }

    // Countly() takes networking_lifecycle_mutex, so the lock order here is
    // instances -> networking. Nothing may acquire them the other way round.
    std::shared_ptr<Countly> fresh(new Countly());
    _instances.push_back(std::make_pair(name, fresh));
    if (created_out != nullptr) {
      *created_out = true;
    }
    if (name.empty()) {
      default_instance_cache.store(fresh.get(), std::memory_order_release);
    }
    return fresh.get();
  }

  Countly *find(const std::string &name) {
    std::lock_guard<std::mutex> lk(_instances_mutex);
    for (size_t index = 0; index < _instances.size(); index++) {
      if (_instances[index].first == name) {
        return _instances[index].second.get();
      }
    }
    return nullptr;
  }

  /**
   * Unregisters 'name' and hands its instance back so the caller can let it drop
   * *outside* the lock. ~Countly calls releasePath(), so destroying while
   * _instances_mutex is held would be a latent self-deadlock.
   */
  std::shared_ptr<Countly> detach(const std::string &name) {
    std::lock_guard<std::mutex> lk(_instances_mutex);
    for (std::vector<std::pair<std::string, std::shared_ptr<Countly>>>::iterator it = _instances.begin(); it != _instances.end(); ++it) {
      if (it->first == name) {
        std::shared_ptr<Countly> found = it->second;
        _instances.erase(it);
        if (name.empty()) {
          default_instance_cache.store(nullptr, std::memory_order_release);
        }
        return found;
      }
    }
    return std::shared_ptr<Countly>();
  }

  /** Destroys every instance in reverse creation order, outside the lock. */
  void clear() {
    std::vector<std::shared_ptr<Countly>> doomed;
    {
      std::lock_guard<std::mutex> lk(_instances_mutex);
      default_instance_cache.store(nullptr, std::memory_order_release);
      for (size_t index = _instances.size(); index > 0; index--) {
        doomed.push_back(_instances[index - 1].second);
      }
      _instances.clear();
    }
    for (size_t index = 0; index < doomed.size(); index++) {
      doomed[index].reset(); // ~Countly runs here, with no registry lock held
    }
  }

  /** @return false if the path is already claimed by a live instance. */
  bool tryClaimPath(const std::string &normalized) {
    std::lock_guard<std::mutex> lk(_paths_mutex);
    return _claimed_paths.insert(normalized).second;
  }

  void releasePath(const std::string &normalized) {
    std::lock_guard<std::mutex> lk(_paths_mutex);
    _claimed_paths.erase(normalized);
  }

  size_t claimedPathCount() {
    std::lock_guard<std::mutex> lk(_paths_mutex);
    return _claimed_paths.size();
  }

private:
  // Two mutexes, never nested in the other direction: _paths_mutex is always the
  // innermost lock, because start() claims a path while holding the instance's
  // own mutex.
  std::mutex _instances_mutex;
  std::vector<std::pair<std::string, std::shared_ptr<Countly>>> _instances;
  std::mutex _paths_mutex;
  std::set<std::string> _claimed_paths;
};

InstanceRegistry &registry() {
  static InstanceRegistry instance;
  return instance;
}

} // namespace

void Countly::inheritDefaultLogger(Countly &instance) {
  Countly *defaultInstance = registry().find(DEFAULT_INSTANCE_NAME);
  if (defaultInstance == nullptr || defaultInstance == &instance) {
    return;
  }
  // LoggerModule serialises this against a concurrent setLogger().
  LoggerFunction callback = defaultInstance->logger->getLogger();
  if (callback != nullptr) {
    instance.logger->setLogger(callback);
  }
}

void Countly::releaseDatabasePathClaim() {
#ifdef COUNTLY_USE_SQLITE
  if (!claimed_database_path.empty()) {
    registry().releasePath(claimed_database_path);
    claimed_database_path.clear();
  }
#endif
}

Countly &Countly::getInstance() {
  // Lock-free once the default instance exists, which is the overwhelmingly
  // common case. Only the very first call, or the first after halt() or
  // destroyInstance(""), pays for the registry lookup.
  Countly *cached = default_instance_cache.load(std::memory_order_acquire);
  if (cached != nullptr) {
    return *cached;
  }
  return getInstance(DEFAULT_INSTANCE_NAME);
}

Countly &Countly::getInstance(const std::string &name) {
  bool created = false;
  Countly *instance = registry().getOrCreate(name, &created);
  if (created && !name.empty()) {
    inheritDefaultLogger(*instance);
    instance->log(LogLevel::WARNING, "[Countly] getInstance, creating a new, uninitialized instance named '" + name + "'; if you expected an existing instance, check the name.");
  }
  return *instance;
}

Countly &Countly::createInstance(const std::string &name) {
  bool created = false;
  Countly *instance = registry().getOrCreate(name, &created);
  if (created) {
    inheritDefaultLogger(*instance);
  } else {
    instance->log(LogLevel::WARNING, "[Countly] createInstance, an instance named '" + name + "' already exists; returning the existing one.");
  }
  return *instance;
}

Countly *Countly::findInstance(const std::string &name) { return registry().find(name); }

bool Countly::hasInstance(const std::string &name) { return findInstance(name) != nullptr; }

void Countly::destroyInstance(const std::string &name) {
  std::shared_ptr<Countly> doomed = registry().detach(name);
  doomed.reset(); // ~Countly runs here, with no registry lock held
}

void Countly::destroyAllInstances() { registry().clear(); }

void Countly::shutdownNetworking() {
  // Resolve the log target before taking networking_lifecycle_mutex. Countly()
  // is constructed while the registry holds its instance mutex and then takes
  // the networking mutex, so acquiring them in the opposite order here would
  // deadlock. Finish with the registry first, then lock.
  Countly *defaultInstance = registry().find(DEFAULT_INSTANCE_NAME);

  int live = 0;
  int fetches = 0;
  {
    // Held across the check and the release so no instance can be constructed
    // in between and have networking torn down underneath it.
    std::lock_guard<std::mutex> lk(networking_lifecycle_mutex);
    live = live_instance_count.load();
    // A remote-config fetch is detached and may outlive the instance that started
    // it, so an instance count of zero is not on its own proof that nobody is
    // inside libcurl.
    fetches = inflight_remote_config_fetches.load();
    if (live == 0 && fetches == 0) {
      RequestModule::releaseGlobalNetworking();
    }
  }

  if (defaultInstance != nullptr) {
    if (live > 0) {
      defaultInstance->log(LogLevel::ERROR, "[Countly] shutdownNetworking, " + std::to_string(live) + " instance(s) are still live; refusing to tear down networking.");
    } else if (fetches > 0) {
      defaultInstance->log(LogLevel::ERROR, "[Countly] shutdownNetworking, " + std::to_string(fetches) + " remote config fetch(es) are still in flight; refusing to tear down networking.");
    }
  }
}

#ifdef COUNTLY_BUILD_TESTS
void Countly::halt() {
  destroyAllInstances();
  getInstance(); // leave a fresh default instance behind, as the old halt() did
}

size_t Countly::debugClaimedPathCount() { return registry().claimedPathCount(); }

int Countly::debugLiveInstanceCount() { return live_instance_count.load(); }
#endif

/**
 * Set limit for the number of requests that can be stored locally.
 * @param requestQueueSize: max size of request queue
 */
void Countly::setMaxRequestQueueSize(unsigned int requestQueueSize) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxRequestQueueSize, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->requestQueueThreshold = requestQueueSize;
}

/**
 * Set limit for the number of requests that can be processed at a time.
 * If the limit is reached, the rest of the requests will be processed in the next cycle.
 * @param batchSize: max size of requests to process at a time
 */
void Countly::setMaxRQProcessingBatchSize(unsigned int batchSize) {
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxProcessingBatchSize = batchSize;
}

void Countly::alwaysUsePost(bool value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] alwaysUsePost, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->forcePost = value;
}

void Countly::setSalt(const std::string &value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setSalt, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->salt = value;
}

void Countly::setLogger(void (*fun)(LogLevel level, const std::string &message)) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setLogger, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  logger->setLogger(fun);
}

void Countly::setHTTPClient(HTTPClientFunction fun) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setHTTPClient, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->http_client_function = fun;
}

void Countly::setSha256(SHA256Function fun) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setSha256, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->sha256_function = fun;
}

/**
 * Enable manual session handling.
 */
void Countly::enableManualSessionControl() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] enableManualSessionControl, You can not enable manual session control after SDK initialization.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->manualSessionControl = true;
}

/**
 * Disable automatic events on user properties changes.
 */
void Countly::disableAutoEventsOnUserProperties() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] disableAutoEventsOnUserProperties, You can not disable automatic events on user properties after SDK initialization.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->autoEventsOnUserProperties = false;
}

void Countly::enableImmediateRequestOnStop() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][enableImmediateRequestOnStop] You can not enable immediate request on stop after SDK initialization.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->immediateRequestOnStop = true;
}

void Countly::setMaxKeyLength(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxKeyLength, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxKeyLength = value;
}

void Countly::setMaxValueSize(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxValueSize, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxValueSize = value;
}

void Countly::setMaxSegmentationValues(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxSegmentationValues, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxSegmentationValues = value;
}

void Countly::setMaxBreadcrumbCount(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxBreadcrumbCount, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->breadcrumbsThreshold = value;
}

void Countly::setMaxStackTraceLinesPerThread(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxStackTraceLinesPerThread, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxStackTraceLinesPerThread = value;
}

void Countly::setMaxStackTraceLineLength(unsigned int value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMaxStackTraceLineLength, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->maxStackTraceLineLength = value;
}

void Countly::setMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setMetrics, This method can't be called after SDK initialization. Returning.");
    return;
  }

  if (!os.empty()) {
    configuration->metrics["_os"] = os;
  }
  if (!os_version.empty()) {
    configuration->metrics["_os_version"] = os_version;
  }

  if (!device.empty()) {
    configuration->metrics["_device"] = device;
  }

  if (!resolution.empty()) {
    configuration->metrics["_resolution"] = resolution;
  }

  if (!carrier.empty()) {
    configuration->metrics["_carrier"] = carrier;
  }

  if (!app_version.empty()) {
    configuration->metrics["_app_version"] = app_version;
  }
}

void Countly::setUserDetails(const std::map<std::string, std::string> &value) {
  // unique_lock so the mutex is released on scope exit, including on a throwing
  // json/map/addRequestToQueue op; unlock/relock around the self-locking flushEvents().
  std::unique_lock<std::mutex> lk(*mutex);
  SDKLimits lim = configurationModule ? configurationModule->getLimits()
                                      : SDKLimits{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  std::map<std::string, std::string> limitedDetails;
  for (const auto &kv : value) {
    unsigned int cap = (kv.first == "picture") ? COUNTLY_MAX_VALUE_SIZE_PICTURE : lim.maxValueSize;
    limitedDetails[kv.first] = cly::limits::truncateString(kv.second, cap);
  }
  session_params["user_details"] = limitedDetails;

  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] setUserDetails, This method can't be called before SDK initialization. Returning.");
    return;
  }

  if (configuration->autoEventsOnUserProperties == true) {
    lk.unlock();
    flushEvents();
    lk.lock();
  }

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"user_details", session_params["user_details"].dump()}};

  requestModule->addRequestToQueue(data);
}

void Countly::setCustomUserDetails(const std::map<std::string, std::string> &value) {
  // unique_lock so the mutex is released on scope exit, including on a throwing
  // json/map/addRequestToQueue op; unlock/re-acquire around the self-locking flushEvents().
  std::unique_lock<std::mutex> lk(*mutex);

  // Determine the post-filter custom property map first, then apply limits.
  std::map<std::string, std::string> customValue;
  SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  if (configurationModule) {
    lim = configurationModule->getLimits();
    auto upFilter = configurationModule->getUserPropertyFilterList();
    if (!upFilter.filterList.empty()) {
      for (const auto &kv : value) {
        bool allowed;
        if (upFilter.isWhitelist) {
          allowed = (upFilter.filterList.find(kv.first) != upFilter.filterList.end());
        } else {
          allowed = (upFilter.filterList.find(kv.first) == upFilter.filterList.end());
        }
        if (allowed) {
          customValue[kv.first] = kv.second;
        }
      }
      if (customValue.empty()) {
        log(LogLevel::DEBUG, "[Countly] setCustomUserDetails, All user properties were filtered out by SBS user property filter.");
        return;
      }
    } else {
      customValue = value;
    }
  } else {
    customValue = value;
  }

  // Apply SDK internal limits: truncate keys/values. No count cap for user
  // properties (that would be the out-of-scope 'upcl').
  std::map<std::string, std::string> limitedCustom;
  for (const auto &kv : customValue) {
    limitedCustom[cly::limits::truncateString(kv.first, lim.maxKeyLength)] = cly::limits::truncateString(kv.second, lim.maxValueSize);
  }
  session_params["user_details"]["custom"] = limitedCustom;

  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] setCustomUserDetails, This method can't be called before SDK initialization. Returning.");
    return;
  }

  if (configuration->autoEventsOnUserProperties == true) {
    lk.unlock();
    flushEvents();
    lk.lock();
  }

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"user_details", session_params["user_details"].dump()}};
  requestModule->addRequestToQueue(data);
}

#pragma region User location

void Countly::setCountry(const std::string &country_code) {
  log(LogLevel::WARNING, "[Countly] setCountry, 'setCountry' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  setLocation(country_code, "", "", "");
}

void Countly::setCity(const std::string &city_name) {
  log(LogLevel::WARNING, "[Countly] setCity, 'setCity' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  setLocation("", city_name, "", "");
}

void Countly::setLocation(double lattitude, double longitude) {
  log(LogLevel::WARNING, "[Countly] setLocation, 'setLocation(latitude, longitude)' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");

  std::ostringstream location_stream;
  location_stream << lattitude << ',' << longitude;
  setLocation("", "", location_stream.str(), "");
}

void Countly::setLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setLocation, SDK is not initialized.");
    return;
  }
  bool isClearingLocation = countryCode.empty() && city.empty() && gpsCoordinates.empty() && ipAddress.empty();
  // unique_lock so the mutex is released on scope exit (incl. throwing json writes);
  // explicit unlock before the self-locking _sendIndependantLocationRequest() below.
  std::unique_lock<std::mutex> lk(*mutex);
  if (!isClearingLocation && configurationModule->isLocationTrackingEnabled() == false) {
    log(LogLevel::ERROR, "[Countly] setLocation, Location tracking is disabled in server configuration, can not set location.");
    return;
  }
  log(LogLevel::INFO, "[Countly] setLocation, Setting location: countryCode = [" + countryCode + "], city = [" + city + "], gpsCoordinates = [" + gpsCoordinates + "], ipAddress = [" + ipAddress + "]");

  if ((!countryCode.empty() && city.empty()) || (!city.empty() && countryCode.empty())) {
    log(LogLevel::WARNING, "[Countly] setLocation, It's required that both 'country_code' and 'city' should be set together");
  }

  session_params["city"] = city;
  session_params["ip_address"] = ipAddress;
  session_params["location"] = gpsCoordinates;
  session_params["country_code"] = countryCode;

  lk.unlock();

  if (is_sdk_initialized) {
    _sendIndependantLocationRequest();
  }
}

void Countly::_sendIndependantLocationRequest() {
  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::DEBUG, "[Countly] _sendIndependantLocationRequest, Start");

  /*
   * Empty country code, city and IP address can not be sent.
   */

  std::map<std::string, std::string> data;

  if (session_params.contains("city") && session_params["city"].is_string() && !session_params["city"].get<std::string>().empty()) {
    data["city"] = session_params["city"].get<std::string>();
  }

  if (session_params.contains("location") && session_params["location"].is_string() && !session_params["location"].get<std::string>().empty()) {
    data["location"] = session_params["location"].get<std::string>();
  }

  if (session_params.contains("country_code") && session_params["country_code"].is_string() && !session_params["country_code"].get<std::string>().empty()) {
    data["country_code"] = session_params["country_code"].get<std::string>();
  }

  if (session_params.contains("ip_address") && session_params["ip_address"].is_string() && !session_params["ip_address"].get<std::string>().empty()) {
    data["ip_address"] = session_params["ip_address"].get<std::string>();
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  data["app_key"] = session_params["app_key"].get<std::string>();
  data["device_id"] = session_params["device_id"].get<std::string>();
  data["timestamp"] = std::to_string(timestamp.count());

  if (data.size() == 3) {
    // No location fields were added — send empty location to clear server-side location
    data["location"] = "";
  }

  requestModule->addRequestToQueue(data);
}

#pragma endregion User location

#pragma region Device Id
void Countly::setDeviceID(const std::string &value, bool same_user) {
  // unique_lock so the mutex is released on scope exit (incl. throwing json writes);
  // explicit unlock before the self-locking _changeDeviceId* helpers below.
  std::unique_lock<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] setDeviceID, Device ID change requested, new value = [" + value + "]");

  if (!session_params.contains("device_id")) {
    session_params["device_id"] = value;
    configuration->deviceId = value;
    log(LogLevel::DEBUG, "[Countly] setDeviceID, No previous device id, assigning initial device id");
    return;
  }

  if (session_params["device_id"].get<std::string>() == value) {
    log(LogLevel::DEBUG, "[Countly] setDeviceID, New device id equals existing device id, ignoring.");
    return;
  }

  lk.unlock();
  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] setDeviceID, Device id can't be changed while the SDK has not been initialized.");
    return;
  }

  if (same_user) {
    _changeDeviceIdWithMerge(value);
  } else {
    _changeDeviceIdWithoutMerge(value);
  }
}

/* Change device ID with merge after SDK has been initialized.*/
void Countly::_changeDeviceIdWithMerge(const std::string &value) {
  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::DEBUG, "[Countly] _changeDeviceIdWithMerge, deviceId = [" + value + "]");

  session_params["old_device_id"] = session_params["device_id"];
  session_params["device_id"] = value;
  configuration->deviceId = value;

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  std::map<std::string, std::string> data = {
      {"app_key", session_params["app_key"].get<std::string>()},
      {"device_id", session_params["device_id"].get<std::string>()},
      {"old_device_id", session_params["old_device_id"].get<std::string>()},
      {"timestamp", std::to_string(timestamp.count())},
  };
  requestModule->addRequestToQueue(data);

  session_params.erase("old_device_id");
}

void Countly::_changeDeviceIdWithoutMerge(const std::string &value) {
  log(LogLevel::DEBUG, "[Countly] _changeDeviceIdWithoutMerge, deviceId = [" + value + "]");

  // send all event to server and end current session of old user
  flushEvents();
  if (configuration->manualSessionControl == false) {
    endSession();
  }

  {
    std::lock_guard<std::mutex> lk(*mutex);
    session_params["device_id"] = value;
    configuration->deviceId = value;
  }

  // start a new session for new user
  if (configuration->manualSessionControl == false) {
    beginSession();
  }
}
#pragma endregion Device Id

void Countly::start(const std::string &app_key, const std::string &host, int port, bool start_thread) {
  // unique_lock so the mutex is released on scope exit (incl. allocation/json
  // throws while constructing modules); unlock/re-acquire around the self-locking
  // configurationModule->fetch*/beginSession() calls below.
  std::unique_lock<std::mutex> lk(*mutex);
  if (is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] start, SDK has already been initialized, 'start' should not be called a second time!");
    return;
  }

#ifdef COUNTLY_USE_SQLITE
  if (configuration->databasePath == "" || configuration->databasePath == " ") {
    log(LogLevel::ERROR, "[Countly] start, Database path can not be empty or blank.");
    return;
  }

  // One instance per database file. Two instances sharing one file would eat
  // each other's queues, and because rows in the `events` table carry no
  // app_key, one instance would pack the other's events into a request stamped
  // with the wrong key.
  const std::string normalized_database_path = cly::utils::normalizeDatabasePath(configuration->databasePath);
  if (!registry().tryClaimPath(normalized_database_path)) {
    log(LogLevel::ERROR, "[Countly] start, Database path '" + configuration->databasePath +
                             "' is already in use by another Countly instance in this process. Note that stop() does not free the path: an instance keeps its claim until it is destroyed. SDK will not be initialized.");
    return;
  }
  claimed_database_path = normalized_database_path;
#endif

  log(LogLevel::INFO, "[Countly] start, Initializing SDK");

#ifdef COUNTLY_USE_SQLITE
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_SQLITE' is defined");
#else
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_SQLITE' is not defined");
#endif

#ifdef COUNTLY_USE_CUSTOM_HTTP
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_CUSTOM_HTTP' is defined");
#else
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_CUSTOM_HTTP' is not defined");
#endif

#ifdef COUNTLY_USE_CUSTOM_SHA256
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_CUSTOM_SHA256' is defined");
#else
  log(LogLevel::INFO, "[Countly] start, 'COUNTLY_USE_CUSTOM_SHA256' is not defined");
#endif

#ifdef _WIN32
  log(LogLevel::INFO, "[Countly] start, '_WIN32' is defined");
#else
  log(LogLevel::INFO, "[Countly] start, '_WIN32' is not defined");
#endif

  enable_automatic_session = start_thread;
  start_thread = true;

  if (port < 0 || port > 65535) {
    log(LogLevel::WARNING, "[Countly] start, Port number is out of valid boundaries. Setting it to 0.");
    port = 0;
  }

  configuration->port = port;
  configuration->appKey = app_key;
  configuration->serverUrl = host;

  session_params["app_key"] = app_key;

#ifdef COUNTLY_USE_SQLITE
  storageModule.reset(new StorageModuleDB(configuration, logger));
#else
  storageModule.reset(new StorageModuleMemory(configuration, logger));
#endif
  storageModule->init();

  requestBuilder.reset(new RequestBuilder(configuration, logger));
  requestModule.reset(new RequestModule(configuration, logger, requestBuilder, storageModule));
  configurationModule.reset(new cly::ConfigurationModule(this, configuration, logger, requestBuilder, storageModule, requestModule, mutex));
  crash_module.reset(new cly::CrashModule(configuration, logger, requestModule, mutex));
  views_module.reset(new cly::ViewsModule(this, logger));

  requestModule->setConfigurationProvider(configurationModule);
  views_module->setConfigurationProvider(configurationModule);
  crash_module->setConfigurationProvider(configurationModule);

  bool result = true;
#ifdef COUNTLY_USE_SQLITE
  result = createEventTableSchema();
  if (!result) {
    log(LogLevel::ERROR, "[Countly] start, Failed to initialize database at path: '" + configuration->databasePath + "'. SDK will not be initialized. Please verify the path is valid and writable.");
  }
#endif

  is_sdk_initialized = result; // after this point SDK is initialized.
  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] start, SDK initialization failed.");
    releaseDatabasePathClaim();
    return;
  }

  if (is_sdk_initialized) {
    lk.unlock();
    configurationModule->fetchConfigFromStorage();
    configurationModule->fetchConfigFromServer(session_params);
    configurationModule->startServerConfigUpdateTimer(session_params);
    lk.lock();
  }

  if (!running) {

    if (configuration->manualSessionControl == false) {
      lk.unlock();
      beginSession();
      lk.lock();
    }

    if (start_thread) {
      stop_thread = false;

      try {
        thread.reset(new std::thread(&Countly::updateLoop, this));
      } catch (const std::system_error &e) {
        std::ostringstream log_message;
        log_message << "[Countly] start, Could not create thread: " << e.what();
        log(LogLevel::FATAL, log_message.str());
      }
    }
  }
}

/**
 * startOnCloud is deprecated and this is going to be removed in the future.
 */
void Countly::startOnCloud(const std::string &app_key) {
  log(LogLevel::WARNING, "[Countly] startOnCloud, 'startOnCloud' is deprecated, this is going to be removed in the future.");
  this->start(app_key, "https://cloud.count.ly", 443);
}

bool Countly::startRemoteConfigFetch(const std::map<std::string, std::string> &data, bool merge, const char *caller) {
  // Checked here rather than on the fetch thread: see RemoteConfigFetch on why it
  // must not hold the configuration module.
  if (configurationModule->isNetworkingEnabled() == false) {
    log(LogLevel::ERROR, std::string("[Countly] ") + caller + ", Error fetching remote config, networking is disabled in SBS");
    return false;
  }

  // exchange, not load-then-store: two threads calling updateRemoteConfig at the
  // same moment must not both get through.
  if (remote_config_store->fetch_running.exchange(true)) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", a remote config fetch is already in flight; ignoring this call.");
    return false;
  }

  std::shared_ptr<RemoteConfigFetch> fetch(new RemoteConfigFetch());
  fetch->requestModule = requestModule;
  fetch->requestBuilder = requestBuilder;
  fetch->logger = logger;
  fetch->store = remote_config_store;
  fetch->data = data;
  fetch->merge = merge;

  inflight_remote_config_fetches.fetch_add(1);
  try {
    // Detached on purpose: nothing may wait for an HTTP round trip, least of all
    // ~Countly. The body holds `fetch` and therefore everything it touches.
    std::thread(runRemoteConfigFetch, fetch).detach();
  } catch (const std::system_error &e) {
    inflight_remote_config_fetches.fetch_sub(1);
    remote_config_store->fetch_running.store(false);
    log(LogLevel::ERROR, std::string("[Countly] ") + caller + ", could not create remote config thread: " + e.what());
    return false;
  }
  return true;
}

void Countly::stop() {
  // Deliberately unchanged from before multi-instance support. Joining the SDK
  // Behavior Settings timer or an in-flight remote-config fetch here would make
  // stop() block where it previously returned at once, and integrators call it
  // on the UI thread at shutdown (see examples/qt_demo). Both joins belong in
  // ~Countly, which is where the lifetime problem they solve actually arises.
  _deleteThread();
  if (configuration->manualSessionControl == false) {
    endSession();
  }
}

void Countly::_deleteThread() {
  {
    std::lock_guard<std::mutex> lk(*mutex);
    stop_thread = true;
  }
  stop_cv.notify_one();
  if (thread && thread->joinable()) {
    try {
      thread->join();
    } catch (const std::system_error &e) {
      log(LogLevel::WARNING, "[Countly] _deleteThread, Could not join thread");
    }
    thread.reset();
  }
}

void Countly::setUpdateInterval(size_t milliseconds) {
  {
    std::lock_guard<std::mutex> lk(*mutex);
    wait_milliseconds = milliseconds;
  }
  if (configuration->immediateRequestOnStop) {
    stop_cv.notify_one();
  }
}

void Countly::addEvent(const cly::Event &event) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] addEvent, SDK is not initialized.");
    return;
  }

  std::string eventKey = event.getKey();
  bool isInternalEvent = eventKey.find("[CLY]_") == 0;

  // Check custom event tracking (only blocks custom events)
  if (!configurationModule->isCustomEventTrackingEnabled() && !isInternalEvent) {
    log(LogLevel::DEBUG, "[Countly] addEvent, custom event tracking is disabled in server configuration, can not add event with key: [" + eventKey + "]");
    return;
  }

  // Apply event filter (only for custom events)
  if (!isInternalEvent) {
    auto filter = configurationModule->getEventFilterList();
    if (!filter.filterList.empty()) {
      bool blocked = false;
      if (filter.isWhitelist) {
        blocked = (filter.filterList.find(eventKey) == filter.filterList.end());
      } else {
        blocked = (filter.filterList.find(eventKey) != filter.filterList.end());
      }
      if (blocked) {
        log(LogLevel::DEBUG, "[Countly] addEvent, event filtered out by SBS event filter: [" + eventKey + "]");
        return;
      }
    }
  }

  // Copy the event so we can apply segmentation filters without modifying the caller's object
  cly::Event filteredEvent = event;

  // Apply segmentation filters
  if (filteredEvent.hasSegmentation()) {
    try {
      auto applySegFilter = [&filteredEvent](const std::set<std::string> &filterKeys, bool isWhitelist) {
        if (filterKeys.empty()) {
          return;
        }
        if (isWhitelist) {
          nlohmann::json seg = nlohmann::json::parse(filteredEvent.serialize())["segmentation"];
          for (auto it = seg.begin(); it != seg.end(); ++it) {
            if (filterKeys.find(it.key()) == filterKeys.end()) {
              filteredEvent.removeSegmentation(it.key());
            }
          }
        } else {
          for (const auto &key : filterKeys) {
            filteredEvent.removeSegmentation(key);
          }
        }
      };

      // Global segmentation filter (sb/sw)
      auto segFilter = configurationModule->getSegmentationFilterList();
      applySegFilter(segFilter.filterList, segFilter.isWhitelist);

      // Event-specific segmentation filter (esb/esw)
      auto eSegFilter = configurationModule->getEventSegmentationFilterList();
      if (!eSegFilter.filterList.empty()) {
        auto mapIt = eSegFilter.filterList.find(eventKey);
        if (mapIt != eSegFilter.filterList.end()) {
          applySegFilter(mapIt->second, eSegFilter.isWhitelist);
        }
      }
    } catch (const std::exception &e) {
      log(LogLevel::ERROR, "[Countly] addEvent, error applying segmentation filter: [" + std::string(e.what()) + "]");
    }
  }

  // Apply SDK internal limits to developer-supplied events only. Internal
  // [CLY]_* events are limited at their own module boundary (e.g. views).
  if (!isInternalEvent) {
    SDKLimits limits = configurationModule->getLimits();
    filteredEvent.applyLimits(limits.maxKeyLength, limits.maxValueSize, limits.maxSegmentationValues);
  }

  std::unique_lock<std::mutex> lk(*mutex);
#ifndef COUNTLY_USE_SQLITE
  event_queue.push_back(filteredEvent.serialize());
#else
  addEventToSqlite(filteredEvent);
#endif
  lk.unlock();
  checkAndSendEventToRQ();
}

void Countly::checkAndSendEventToRQ() {
  nlohmann::json events = nlohmann::json::array();
  int queueSize = checkEQSize();
  // if queue size could not be get return early
  if (queueSize < 0) {
    return;
  }
  // unique_lock so the mutex is released on scope exit, including when json::parse
  // on a queued event or sendEventsToRQ throws; unlock/re-acquire around the
  // self-locking fillEventsIntoJson().
  std::unique_lock<std::mutex> lk(*mutex);
#ifdef COUNTLY_USE_SQLITE
  if (queueSize >= configurationModule->getEventQueueSizeLimit()) {
    log(LogLevel::DEBUG, "[Countly] checkAndSendEventToRQ, Event queue threshold is reached");
    std::string event_ids;

    // fetch events up to the threshold from the database
    lk.unlock();
    fillEventsIntoJson(events, event_ids);
    lk.lock();
    // send them to request queue
    sendEventsToRQ(events);
    // remove them from database
    removeEventWithId(event_ids);
  }
#else
  if (queueSize >= configurationModule->getEventQueueSizeLimit()) {
    log(LogLevel::WARNING, "[Countly] checkAndSendEventToRQ, Event queue is full, dropping the oldest event to insert a new one");
    for (const auto &event_json : event_queue) {
      events.push_back(nlohmann::json::parse(event_json));
    }
    sendEventsToRQ(events);
    event_queue.clear();
  }
#endif
}

void Countly::setMaxEvents(size_t value) {
  log(LogLevel::WARNING, "[Countly] setMaxEvents, 'setMaxEvents' and 'SetMaxEventsPerMessage' are deprecated. Use 'setEventsToRQThreshold' instead.");
  setEventsToRQThreshold(static_cast<int>(value));
}

void Countly::setEventsToRQThreshold(int value) {
  log(LogLevel::DEBUG, "[Countly] setEventsToRQThreshold, Given threshold:[" + std::to_string(value) + "]");
  std::unique_lock<std::mutex> lk(*mutex);
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setEventsToRQThreshold, Threshold can not be less than 1. Setting it to 1 instead of:[" + std::to_string(value) + "]");
    value = 1;
  } else if (value > 10000) {
    log(LogLevel::WARNING, "[Countly] setEventsToRQThreshold, Threshold can not be greater than 10000. Setting it to 10000 instead of:[" + std::to_string(value) + "]");
    value = 10000;
  }

  // set the value
  configuration->eventQueueThreshold = value;
  // if current queue size is greater than the new threshold, send events to RQ
  lk.unlock();
  checkAndSendEventToRQ();
}

void Countly::flushEvents(std::chrono::seconds timeout) {
  log(LogLevel::DEBUG, "[Countly] flushEvents, timeout: [" + std::to_string(timeout.count()) + "] seconds");

  try {
    auto wait_duration = std::chrono::seconds(1);
    bool update_failed;

    // Try to update session
    while (timeout.count() != 0) {

      // try to update session if event queue is not empty
      update_failed = attemptSessionUpdateEQ();

      // if update is successful or EQ is empty, break the loop
      if (!update_failed) {
        break;
      }

      // wait for a while
      std::this_thread::sleep_for(wait_duration);
      // increase wait/retry duration	(exponential backoff: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, ...)
      wait_duration *= 2;
      // if wait/retry time is bigger than timeout stop trying, else decrease timeout
      timeout = (wait_duration > timeout) ? std::chrono::seconds(0) : (timeout - wait_duration);
    }

    // Clear the event queue
    clearEQInternal(); // TODO: Check if this is necessary

    // TODO: Check if we capture anything other than a system_error
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] flushEvents, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
}

bool Countly::attemptSessionUpdateEQ() {
  // return false if event queue is empty
#ifndef COUNTLY_USE_SQLITE
  {
    std::lock_guard<std::mutex> lk(*mutex);
    if (event_queue.empty()) {
      return false;
    }
  }
#else
  int event_count = checkEQSize();
  if (event_count <= 0) {
    return false;
  }
#endif
  if (configuration->manualSessionControl == false) {
    return !updateSession();
  } else {
    packEvents();
    return false;
  }
}

void Countly::clearEQInternal() {
#ifndef COUNTLY_USE_SQLITE
  // event_queue is guarded by mutex; the only caller (flushEvents) holds no lock here.
  std::lock_guard<std::mutex> lk(*mutex);
  event_queue.clear();
#else
  clearPersistentEQ();
#endif
}

#ifdef COUNTLY_BUILD_TESTS
std::vector<std::string> Countly::debugReturnStateOfEQ() {
  try {

#ifdef COUNTLY_USE_SQLITE
    std::vector<std::string> v;
    sqlite3 *database;
    int return_value, row_count, column_count;
    char **table;
    char *error_message;

    return_value = cly::utils::openDatabase(configuration->databasePath, &database);
    if (return_value == SQLITE_OK) {
      std::ostringstream sql_statement_stream;
      sql_statement_stream << "SELECT * FROM events ORDER BY evtid ASC;";
      std::string sql_statement = sql_statement_stream.str();

      return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
      bool no_request = (row_count == 0);
      if (return_value == SQLITE_OK && !no_request) {

        for (int event_index = 1; event_index < row_count + 1; event_index++) {
          std::string rqstId = table[event_index * column_count];
          std::string rqst = table[(event_index * column_count) + 1];
          v.push_back(rqst);
        }

      } else if (return_value != SQLITE_OK) {
        std::string error(error_message);
        sqlite3_free(error_message);
      }
      sqlite3_free_table(table);
    }
    sqlite3_close(database);
#else
    std::lock_guard<std::mutex> lk(*mutex);
    std::vector<std::string> v(event_queue.begin(), event_queue.end());
#endif
    return v;
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] debugReturnStateOfEQ, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
    // Without this the function falls off its end and the returned vector is
    // indeterminate.
    return std::vector<std::string>();
  }
}

void Countly::debugInjectRawEvent(const std::string &raw) {
  std::lock_guard<std::mutex> lk(*mutex);
#ifndef COUNTLY_USE_SQLITE
  event_queue.push_back(raw);
#else
  (void)raw;
  log(LogLevel::WARNING, "[Countly] debugInjectRawEvent, not supported in SQLite builds.");
#endif
}
#endif

bool Countly::beginSession() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] beginSession, SDK is not initialized.");
    return false;
  }
  // unique_lock so the mutex is always released on scope exit, including the
  // early returns below and any exception (e.g. a json type_error from a
  // session_params access, or addRequestToQueue) thrown while it is held.
  std::unique_lock<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] beginSession, Starting session");
  if (configurationModule->isSessionTrackingEnabled() == false) {
    log(LogLevel::ERROR, "[Countly] beginSession, Session tracking is disabled in server configuration, can not begin session.");
    return false;
  }
  if (began_session == true) {
    log(LogLevel::DEBUG, "[Countly] beginSession, Session is already active.");
    return true;
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::map<std::string, std::string> data = {{"sdk_name", COUNTLY_SDK_NAME}, {"sdk_version", COUNTLY_SDK_VERSION}, {"timestamp", std::to_string(timestamp.count())}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()},
                                             {"begin_session", "1"}};

  if (session_params.contains("city") && session_params["city"].is_string() && !session_params["city"].get<std::string>().empty()) {
    data["city"] = session_params["city"].get<std::string>();
  }

  if (session_params.contains("location") && session_params["location"].is_string() && !session_params["location"].get<std::string>().empty()) {
    data["location"] = session_params["location"].get<std::string>();
  }

  if (session_params.contains("country_code") && session_params["country_code"].is_string() && !session_params["country_code"].get<std::string>().empty()) {
    data["country_code"] = session_params["country_code"].get<std::string>();
  }

  if (session_params.contains("ip_address") && session_params["ip_address"].is_string() && !session_params["ip_address"].get<std::string>().empty()) {
    data["ip_address"] = session_params["ip_address"].get<std::string>();
  }

  if (session_params.contains("user_details")) {
    data["user_details"] = session_params["user_details"].dump();
    session_params.erase("user_details");
  }

  if (configuration->metrics.size() > 0) {
    data["metrics"] = configuration->metrics.dump();
  }

  requestModule->addRequestToQueue(data);
  session_params.erase("user_details");
  last_sent_session_request = Countly::getTimestamp();
  began_session = true;
  // snapshot guarded state before releasing the lock
  bool shouldUpdateRemoteConfig = remote_config_enabled;
  lk.unlock();

  if (shouldUpdateRemoteConfig) {
    updateRemoteConfig();
  }
  return true;
}

/**
 * @brief Update session
 */
bool Countly::updateSession() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateSession, SDK is not initialized.");
    return false;
  }
  // unique_lock so the mutex is always released on scope exit, including when an
  // exception propagates out of a call made while the lock is held.
  std::unique_lock<std::mutex> lk(*mutex, std::defer_lock);
  try {
    // Check if there was a session, if not try to start one
    lk.lock();
    if (configurationModule->isSessionTrackingEnabled() == false) {
      log(LogLevel::ERROR, "[Countly] updateSession, Session tracking is disabled in server configuration, can not update session.");
      lk.unlock();
      return false;
    }
    if (began_session == false) {
      lk.unlock();
      if (configuration->manualSessionControl == true) {
        log(LogLevel::WARNING, "[Countly] updateSession, SDK is in manual session control mode and there is no active session. Please start a session first.");
        return false;
      }
      if (!beginSession()) {
        log(LogLevel::DEBUG, "[Countly] updateSession, Failed to begin session.");
        // if beginSession fails, we should not try to update session
        return false;
      }
      lk.lock();
      began_session = true;
    }

    // events array
    nlohmann::json events = nlohmann::json::array();
    std::string event_ids;
    lk.unlock();
    bool no_events = checkEQSize() > 0 ? false : true;
    lk.lock();

    if (!no_events) {
#ifndef COUNTLY_USE_SQLITE
      for (const auto &event_json : event_queue) {
        events.push_back(nlohmann::json::parse(event_json));
      }
#else
      // TODO: If database_path was empty there was return false here
      lk.unlock();
      fillEventsIntoJson(events, event_ids);
      lk.lock();
#endif
    } else {
      log(LogLevel::DEBUG, "[Countly] updateSession, EQ empty.");
    }
    lk.unlock();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
    lk.lock();

    // report session duration if it is greater than the configured session duration value
    if (duration.count() >= configurationModule->getSessionUpdateInterval()) {
      log(LogLevel::DEBUG, "[Countly] updateSession, Sending session update.");
      std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}};
      requestModule->addRequestToQueue(data);

      last_sent_session_request += duration;
    }

    // report events if there are any to request queue
    if (!no_events) {
      sendEventsToRQ(events);
    }

// clear event queue
// TODO: check if we want to totally wipe the event queue in memory but not in database
#ifndef COUNTLY_USE_SQLITE
    event_queue.clear();
#else
    if (!event_ids.empty()) {
      // this is a partial clearance, we only remove the events that were sent
      removeEventWithId(event_ids);
    }
#endif
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] updateSession, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
  // lk releases the mutex on scope exit if it is still held.
  return true;
}

void Countly::packEvents() {
  // unique_lock so the mutex is always released on scope exit, including when an
  // exception propagates out of a call made while the lock is held.
  std::unique_lock<std::mutex> lk(*mutex, std::defer_lock);
  try {
    lk.lock();
    // events array
    nlohmann::json events = nlohmann::json::array();
    std::string event_ids;
    lk.unlock();
    bool no_events = checkEQSize() > 0 ? false : true;
    lk.lock();

    if (!no_events) {
#ifndef COUNTLY_USE_SQLITE
      for (const auto &event_json : event_queue) {
        events.push_back(nlohmann::json::parse(event_json));
      }
#else
      // TODO: If database_path was empty there was return false here
      lk.unlock();
      fillEventsIntoJson(events, event_ids);
      lk.lock();
#endif
    } else {
      log(LogLevel::DEBUG, "[Countly] packEvents, EQ empty.");
    }
    // report events if there are any to request queue
    if (!no_events) {
      sendEventsToRQ(events);
    }

// clear event queue
// TODO: check if we want to totally wipe the event queue in memory but not in database
#ifndef COUNTLY_USE_SQLITE
    event_queue.clear();
#else
    if (!event_ids.empty()) {
      // this is a partial clearance, we only remove the events that were sent
      removeEventWithId(event_ids);
    }
#endif
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] packEvents, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
  // lk releases the mutex on scope exit if it is still held.
}

void Countly::sendEventsToRQ(const nlohmann::json &events) {
  // Every caller decides to send based on an event queue size it read earlier,
  // outside the lock. Another thread can drain the queue in between, and then
  // there is nothing to send: queueing an "events=[]" request would spend a
  // request on nothing.
  if (events.empty()) {
    log(LogLevel::DEBUG, "[Countly] sendEventsToRQ, No events to send.");
    return;
  }

  log(LogLevel::DEBUG, "[Countly] sendEventsToRQ, Sending events to RQ.");
  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"events", events.dump()}};
  requestModule->addRequestToQueue(data);
}

bool Countly::endSession() {
  if (!is_sdk_initialized && !is_being_disposed) {
    log(LogLevel::WARNING, "[Countly] endSession, SDK is not initialized.");
    return false;
  }
  log(LogLevel::INFO, "[Countly] endSession, Ending session");
  if (is_being_disposed == false && configurationModule->isSessionTrackingEnabled() == false) {
    log(LogLevel::ERROR, "[Countly] endSession, Session tracking is disabled in server configuration, can not end session.");
    return false;
  }
  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  // lock_guard so the mutex is released on scope exit, including the early
  // returns below and any exception (e.g. a json type_error from a session_params
  // access, or addRequestToQueue) thrown while it is held.
  //
  // The began_session check belongs inside this lock, together with the write
  // that clears it: read outside, two concurrent calls could both see an active
  // session and both queue an end_session request. stop() on one thread and a
  // device id change on another is enough to get there.
  std::lock_guard<std::mutex> lk(*mutex);
  if (began_session == false) {
    log(LogLevel::DEBUG, "[Countly] endSession, There is no active session to end.");
    return true;
  }

  // getSessionDuration() takes this same mutex, so compute it inline here.
  const auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_sent_session_request);

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}, {"timestamp", std::to_string(timestamp.count())}, {"end_session", "1"}};

  if (is_being_disposed) {
    // if SDK is being destroyed, don't attempt to send the end-session request.
    return false;
  }

  requestModule->addRequestToQueue(data);

  last_sent_session_request = now;
  began_session = false;
  return true;
}

std::chrono::system_clock::time_point Countly::getTimestamp() { return std::chrono::system_clock::now(); }

int Countly::checkEQSize() {
  log(LogLevel::DEBUG, "[Countly] checkEQSize, Start");
  int event_count = -1;
  if (!is_sdk_initialized) {
    log(LogLevel::DEBUG, "[Countly] checkEQSize, This method can't be called before SDK initialization.");
    return event_count;
  }

  // The event queue is guarded by the instance mutex, which the SDK holds while
  // it invokes the log callback, so answering a call made from inside that
  // callback would deadlock. -1 is the same "cannot determine" answer this
  // returns before initialization. checkRQSize() has no such restriction.
  if (LoggerModule::isInsideCallback()) {
    return event_count;
  }

#ifdef COUNTLY_USE_SQLITE
  event_count = checkPersistentEQSize();
#else
  event_count = checkMemoryEQSize();
#endif
  return event_count;
}

int Countly::checkRQSize() {
  log(LogLevel::DEBUG, "[Countly] checkRQSize, Start");
  int request_count = -1;
  if (!is_sdk_initialized) {
    log(LogLevel::DEBUG, "[Countly] checkRQSize, SDK is not initialized.");
    return request_count;
  }

  // Deliberately does not take the instance mutex. The storage modules serialise
  // their own access -- the in-memory one with its own lock, the SQLite one
  // through its per-operation connection and busy timeout -- so the count is safe
  // to read without it. Taking it here would deadlock any integrator who calls
  // this from their log callback, which the SDK invokes from places that hold it.
  request_count = static_cast<int>(requestModule->RQSize());
  return request_count;
}

#ifndef COUNTLY_USE_SQLITE
int Countly::checkMemoryEQSize() {
  log(LogLevel::DEBUG, "[Countly] checkMemoryEQSize, Checking event queue size in memory.");
  // See checkEQSize(): locking here would deadlock a call made from inside the
  // integrator's log callback.
  if (LoggerModule::isInsideCallback()) {
    return -1;
  }

  int result = 0;
  std::lock_guard<std::mutex> lk(*mutex);
  result = static_cast<int>(event_queue.size());
  return result;
}
#endif

// Standalone Sqlite functions
#ifdef COUNTLY_USE_SQLITE
void Countly::removeEventWithId(const std::string &event_ids) {
  // TODO: Check if we should check database_path set or not
  log(LogLevel::DEBUG, "[Countly] removeEventWithId, Removing events from storage: [" + event_ids + "]");

  // fillEventsIntoJson() hands back ")" rather than "()" when it found no rows,
  // and either one builds a malformed "... evtid IN )" statement. Nothing to
  // delete in that case anyway.
  if (event_ids.empty() || event_ids.find_first_of("0123456789") == std::string::npos) {
    log(LogLevel::DEBUG, "[Countly] removeEventWithId, No event ids given, nothing to remove.");
    return;
  }

  sqlite3 *database;
  int return_value;
  char *error_message;

  // we attempt to clear the events in the database only if there were any events collected previously
  return_value = cly::utils::openDatabase(database_path, &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM events WHERE evtid IN " << event_ids << ';';
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::ERROR, "[Countly] removeEventWithId, SQLite error: " + std::string(error_message));
      sqlite3_free(error_message);
    } else {
      log(LogLevel::DEBUG, "[Countly] removeEventWithId, Removed events with the given ID(s).");
    }
  } else {
    log(LogLevel::ERROR, "[Countly] removeEventWithId, Could not open database.");
  }
  sqlite3_close(database);
}

void Countly::fillEventsIntoJson(nlohmann::json &events, std::string &event_ids) {
  // lock_guard so the mutex is released on every exit path, including the early
  // return below and any exception (e.g. nlohmann::json::parse on a corrupt
  // stored row at the loop below) thrown while it is held.
  std::lock_guard<std::mutex> lk(*mutex);
  if (database_path.empty()) {
    log(LogLevel::FATAL, "[Countly] fillEventsIntoJson, SQLite database path is not set.");
    event_ids = "";
    return;
  }

  log(LogLevel::DEBUG, "[Countly] fillEventsIntoJson, Fetching events from storage.");
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  // open database
  return_value = cly::utils::openDatabase(database_path, &database);
  // if database opened successfully
  if (return_value == SQLITE_OK) {

    // create sql statement to fetch events as much as the event queue threshold
    // TODO: check if this is something we want to do
    std::string sql_statement = "SELECT evtid, event FROM events;";

    // execute sql statement
    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      std::ostringstream event_id_stream;
      event_id_stream << '(';

      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        event_id_stream << table[event_index * column_count] << ',';
        // add event to the events array
        events.push_back(nlohmann::json::parse(table[(event_index * column_count) + 1]));
      }

      log(LogLevel::DEBUG, "[Countly] fillEventsIntoJson, Events count = [" + std::to_string(events.size()) + "]");

      event_id_stream.seekp(-1, event_id_stream.cur);
      event_id_stream << ')';

      // write event ids to a string stream (or more like copy out that stream here) to be used in the delete statement
      event_ids = event_id_stream.str();
    } else {
      log(LogLevel::ERROR, "[Countly] fillEventsIntoJson, SQLite error: " + std::string(error_message));
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  } else {
    log(LogLevel::ERROR, "[Countly] fillEventsIntoJson, Could not open database.");
  }
  sqlite3_close(database);
}

int Countly::checkPersistentEQSize() {
  int result = -1;
  // See checkEQSize(): locking here would deadlock a call made from inside the
  // integrator's log callback.
  if (LoggerModule::isInsideCallback()) {
    return result;
  }

  std::unique_lock<std::mutex> lk(*mutex);
  if (database_path.empty()) {
    log(LogLevel::FATAL, "[Countly] checkPersistentEQSize, SQLite database path is not set");
    return result;
  }

  sqlite3 *database;
  int return_value = cly::utils::openDatabase(database_path, &database);
  lk.unlock();

  if (return_value == SQLITE_OK) {
    char *error_message;
    int row_count, column_count;
    char **table;
    return_value = sqlite3_get_table(database, "SELECT COUNT(*) FROM events;", &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      result = atoi(table[1]);
      log(LogLevel::DEBUG, "[Countly] checkPersistentEQSize, Fetched event count from database: [" + std::to_string(result) + "]");
    } else {
      log(LogLevel::ERROR, "[Countly] checkPersistentEQSize, SQLite error: " + std::string(error_message));
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  } else {
    log(LogLevel::WARNING, "[Countly] checkPersistentEQSize, Could not open database");
  }
  sqlite3_close(database);
  return result;
}

void Countly::addEventToSqlite(const cly::Event &event) {
  log(LogLevel::DEBUG, "[Countly] addEventToSqlite, Start");
  try {
    if (database_path.empty()) {
      log(LogLevel::FATAL, "[Countly] addEventToSqlite, Cannot add event, SQLite database path is not set");
      return;
    }

    sqlite3 *database;
    int return_value;
    char *error_message;

    return_value = cly::utils::openDatabase(database_path, &database);
    if (return_value == SQLITE_OK) {
      std::ostringstream sql_statement_stream;
      // TODO Investigate if we need to escape single quotes in serialized event
      sql_statement_stream << "INSERT INTO events (event) VALUES('" << event.serialize() << "');";
      std::string sql_statement = sql_statement_stream.str();

      return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
      if (return_value != SQLITE_OK) {
        // Say which operation failed: this event is being dropped, and a bare
        // "database is locked" gives no hint of what was lost.
        log(LogLevel::ERROR, "[Countly] addEventToSqlite, Event was not stored, SQLite error: " + std::string(error_message));
        sqlite3_free(error_message);
      }
    }
    sqlite3_close(database);
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] addEventToSqlite, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
}

void Countly::clearPersistentEQ() {
  log(LogLevel::DEBUG, "[Countly] clearPersistentEQ, Start");
  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = cly::utils::openDatabase(database_path, &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "DELETE FROM events;", nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::FATAL, "[Countly] clearPersistentEQ, SQLite error: " + std::string(error_message));
      sqlite3_free(error_message);
    } else {
      log(LogLevel::DEBUG, "[Countly] clearPersistentEQ, Cleared event queue");
    }
  }
  sqlite3_close(database);
}

void Countly::setDatabasePath(const std::string &path) {
  if (is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly] setDatabasePath, This method can't be called after SDK initialization. Returning.");
    return;
  }

  if (path == "" || path == " ") {
    log(LogLevel::ERROR, "[Countly] setDatabasePath, Database path can not be empty or blank. Returning.");
    return;
  }

  configuration->databasePath = path;
  log(LogLevel::INFO, "[Countly] setDatabasePath, Setting database path = [" + path + "]");
}

bool Countly::createEventTableSchema() {
  try {
    bool result = false;
    sqlite3 *database;
    int return_value, row_count, column_count;
    char **table;
    char *error_message;

    database_path = configuration->databasePath;

    return_value = cly::utils::openDatabase(database_path, &database);
    if (return_value == SQLITE_OK) {
      return_value = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS events (evtid INTEGER PRIMARY KEY, event TEXT)", nullptr, nullptr, &error_message);
      if (return_value != SQLITE_OK) {
        log(LogLevel::ERROR, "[Countly] createEventTableSchema, SQLite error: " + std::string(error_message));
        sqlite3_free(error_message);
      } else {
        result = true;
      }
    } else {
      const char *error = sqlite3_errmsg(database);
      log(LogLevel::ERROR, "[Countly] createEventTableSchema, Could not open database: " + std::string(error));
      database_path.clear();
    }
    sqlite3_close(database);
    return result;
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "[Countly] createEventTableSchema, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
    // Without this the function falls off its end and hands an indeterminate
    // value to is_sdk_initialized, so a database error could leave the SDK
    // reporting itself as initialized.
    return false;
  }
}
#endif
void Countly::log(LogLevel level, const std::string &message) { logger->log(cly::LogLevel(level), message); }

static size_t countly_curl_write_callback(void *data, size_t byte_size, size_t n_bytes, std::string *body) {
  size_t data_size = byte_size * n_bytes;
  body->append((const char *)data, data_size);
  return data_size;
}

std::string Countly::calculateChecksum(const std::string &salt, const std::string &data) {
  std::string salted_data = data + salt;
#ifdef COUNTLY_USE_CUSTOM_SHA256
  if (configuration->sha256_function == nullptr) {
    log(LogLevel::FATAL, "[Countly] calculateChecksum, Missing SHA 256 function");
    return {};
  }

  return configuration->sha256_function(salted_data);
#else
  unsigned char checksum[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;

  SHA256_Init(&sha256);
  SHA256_Update(&sha256, salted_data.c_str(), salted_data.size());
  SHA256_Final(checksum, &sha256);

  std::ostringstream checksum_stream;
  for (size_t index = 0; index < SHA256_DIGEST_LENGTH; index++) {
    checksum_stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(checksum[index]);
  }

  return checksum_stream.str();
#endif
}

std::chrono::system_clock::duration Countly::getSessionDuration(std::chrono::system_clock::time_point now) {
  std::lock_guard<std::mutex> lk(*mutex);
  std::chrono::system_clock::duration duration = now - last_sent_session_request;
  return duration;
}

std::chrono::system_clock::duration Countly::getSessionDuration() { return Countly::getSessionDuration(Countly::getTimestamp()); }

void Countly::updateLoop() {
  log(LogLevel::DEBUG, "[Countly][updateLoop]");
  {
    std::lock_guard<std::mutex> lk(*mutex);
    running = true;
  }
  try {
    if (configuration->immediateRequestOnStop) {
      while (true) {
        {
          std::unique_lock<std::mutex> lk(*mutex);
          stop_cv.wait_for(lk, std::chrono::milliseconds(wait_milliseconds), [this] {
            return stop_thread.load();
          });
          if (stop_thread) {
            stop_thread = false;
            running = false;
            return;
          }
        }
        if (enable_automatic_session == true && configuration->manualSessionControl == false) {
          updateSession();
        } else if (configuration->manualSessionControl == true) {
          packEvents();
        }
        requestModule->processQueue(mutex);
      }
    } else {
      while (true) {
        size_t last_wait_milliseconds;
        {
          std::lock_guard<std::mutex> lk(*mutex);
          if (stop_thread) {
            stop_thread = false;
            break;
          }
          last_wait_milliseconds = wait_milliseconds;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(last_wait_milliseconds));
        if (enable_automatic_session == true && configuration->manualSessionControl == false) {
          updateSession();
        } else if (configuration->manualSessionControl == true) {
          packEvents();
        }
        requestModule->processQueue(mutex);
      }
      std::lock_guard<std::mutex> lk(*mutex);
      running = false;
    }
  } catch (const std::exception &e) {
    running = false;
    log(LogLevel::ERROR, std::string("[Countly][updateLoop] exception in update loop: ") + e.what());
  } catch (...) {
    running = false;
    log(LogLevel::FATAL, "[Countly][updateLoop] unknown non-std::exception caught, stopping update loop");
  }
}

void Countly::enableRemoteConfig() {
  std::lock_guard<std::mutex> lk(*mutex);
  remote_config_enabled = true;
}

void Countly::updateRemoteConfig() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateRemoteConfig, SDK is not initialized.");
    return;
  }
  std::unique_lock<std::mutex> lk(*mutex);
  if (!session_params["app_key"].is_string() || !session_params["device_id"].is_string()) {

    log(LogLevel::ERROR, "[Countly] updateRemoteConfig, Error updating remote config, app key or device id is missing");
    return;
  }
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  lk.unlock();

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Replaces the stored values wholesale, since this fetches all of them.
  startRemoteConfigFetch(data, false, "updateRemoteConfig");
}

nlohmann::json Countly::getRemoteConfigValue(const std::string &key) {
  // The store's own mutex, not the instance mutex: the fetch that writes these
  // values may outlive the instance, so the two cannot share a lock.
  std::lock_guard<std::mutex> lk(remote_config_store->mutex);
  nlohmann::json value = remote_config_store->values[key];
  return value;
}

void Countly::updateRemoteConfigFor(std::string *keys, size_t key_count) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateRemoteConfigFor, SDK is not initialized.");
    return;
  }
  std::unique_lock<std::mutex> lk(*mutex);
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  {
    nlohmann::json keys_json = nlohmann::json::array();
    for (size_t key_index = 0; key_index < key_count; key_index++) {
      keys_json.push_back(keys[key_index]);
    }
    data["keys"] = keys_json.dump();
  }
  lk.unlock();

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Merges into the stored values, since this fetches only some of them.
  startRemoteConfigFetch(data, true, "updateRemoteConfigFor");
}

void Countly::updateRemoteConfigExcept(std::string *keys, size_t key_count) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateRemoteConfigExcept, SDK is not initialized.");
    return;
  }
  std::unique_lock<std::mutex> lk(*mutex);
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  {
    nlohmann::json keys_json = nlohmann::json::array();
    for (size_t key_index = 0; key_index < key_count; key_index++) {
      keys_json.push_back(keys[key_index]);
    }
    data["omit_keys"] = keys_json.dump();
  }
  lk.unlock();

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Merges into the stored values, since this fetches only some of them.
  startRemoteConfigFetch(data, true, "updateRemoteConfigExcept");
}
} // namespace cly
