#include "countly/internal_limits.hpp"
#include "countly/path_utils.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <chrono>
#include <cmath>
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
#include "platform_info.hpp"

#ifdef COUNTLY_USE_SQLITE
#include "countly/sqlite_utils.hpp"
#include "sqlite3.h"
#endif

namespace cly {
namespace {
const char *const HEALTH_CHECK_AREA_SESSIONS = "sessions";
const char *const HEALTH_CHECK_AREA_EVENTS = "events";

/**
 * Splits an absolute http or https server URL into its origin, the lower case scheme with the host, the port written in
 * it and its path, without trailing '/' characters.
 *
 * @param url: the URL the app gave
 * @param origin: receives the scheme and the host, for example "https://example.com"
 * @param port: receives the port written in the URL, 0 when it has none
 * @param path: receives the path, for example "/countly", empty when the URL has none
 * @param trimmed: set to true when trailing '/' characters were removed
 * @return false when the URL can't be used: another scheme, no host, a port that is not a number from 1 to 65535, a
 *         query, a fragment or white space
 */
bool parseServerUrl(const std::string &url, std::string &origin, unsigned int &port, std::string &path, bool &trimmed) {
  const size_t separator = url.find("://");
  if (separator == std::string::npos) {
    return false;
  }
  std::string scheme = url.substr(0, separator);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (scheme != "http" && scheme != "https") {
    return false;
  }
  std::string rest = url.substr(separator + 3);
  const size_t length = rest.size();
  while (!rest.empty() && rest.back() == '/') {
    rest.pop_back();
  }
  trimmed = rest.size() != length;
  if (rest.empty() || rest.find_first_of(" \t\r\n?#") != std::string::npos) {
    return false;
  }
  const size_t path_start = rest.find('/');
  std::string authority = rest.substr(0, path_start);
  // the colons of an IPv6 address are inside its brackets, so only a colon after them starts the port
  const size_t port_separator = authority.find(':', authority.empty() || authority[0] != '[' ? 0 : authority.find(']'));
  unsigned int url_port = 0;
  if (port_separator != std::string::npos) {
    const std::string digits = authority.substr(port_separator + 1);
    if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos) {
      return false;
    }
    url_port = static_cast<unsigned int>(std::stoul(digits));
    if (url_port < 1 || url_port > 65535) {
      return false;
    }
    authority.erase(port_separator);
  }
  if (authority.empty() || (authority[0] == '[' && authority.back() != ']')) {
    return false;
  }
  origin = scheme + "://" + authority;
  port = url_port;
  path = path_start == std::string::npos ? std::string() : rest.substr(path_start);
  return true;
}

/**
 * Whether the text is an ISO 3166-1 alpha-2 country code in upper case, such as "US".
 */
bool isCountryCode(const std::string &text) { return text.size() == 2 && std::isupper(static_cast<unsigned char>(text[0])) && std::isupper(static_cast<unsigned char>(text[1])); }

/**
 * Whether the text is a decimal number within the range, without anything else around it.
 */
bool isDecimalWithin(const std::string &text, double minimum, double maximum) {
  if (text.empty() || text.find_first_not_of("+-.0123456789") != std::string::npos) {
    return false;
  }
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  return end == text.c_str() + text.size() && value >= minimum && value <= maximum;
}

/**
 * Whether the text is a latitude and a longitude in decimal degrees separated by a comma, such as "52.52001,13.40495".
 */
bool isGpsCoordinates(const std::string &text) {
  const std::string::size_type comma = text.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  std::string latitude = text.substr(0, comma);
  std::string longitude = text.substr(comma + 1);
  latitude.erase(std::remove(latitude.begin(), latitude.end(), ' '), latitude.end());
  longitude.erase(std::remove(longitude.begin(), longitude.end(), ' '), longitude.end());
  return isDecimalWithin(latitude, -90, 90) && isDecimalWithin(longitude, -180, 180);
}

/**
 * Whether the text is an IPv4 address such as "203.0.113.7", or an IPv6 address such as "2001:db8::1".
 */
bool isIpAddress(const std::string &text) {
  if (text.find(':') != std::string::npos) {
    return text.size() <= 45 && text.find_first_not_of("0123456789abcdefABCDEF:.") == std::string::npos;
  }
  std::istringstream parts(text);
  std::string part;
  int count = 0;
  while (std::getline(parts, part, '.')) {
    if (part.empty() || part.size() > 3 || part.find_first_not_of("0123456789") != std::string::npos || std::stoi(part) > 255) {
      return false;
    }
    count++;
  }
  return count == 4 && text.back() != '.';
}

/**
 * Whether the key is one of the internal events the SDK knows. Other "[CLY]_" keys are custom events.
 */
bool isInternalEventKey(const std::string &key) {
  static const char *const internal_keys[] = {"[CLY]_view", "[CLY]_nps", "[CLY]_survey", "[CLY]_star_rating", "[CLY]_orientation", "[CLY]_push_action", "[CLY]_action", "[CLY]_content_shown", "[CLY]_content_interacted"};
  for (const char *internal_key : internal_keys) {
    if (key == internal_key) {
      return true;
    }
  }
  return false;
}

/**
 * Whether the key is one of the internal events that go to the server right away: feedback answers and push actions.
 */
bool isImmediateFlushEventKey(const std::string &key) { return key == "[CLY]_nps" || key == "[CLY]_survey" || key == "[CLY]_star_rating" || key == "[CLY]_push_action"; }

/**
 * Replaces a count that is not a whole number from 1 to INT_MAX with 1 and removes a negative duration, logging
 * each as a warning counted as log code w750.
 */
void validateEventValues(nlohmann::json &object, const std::string &key, LoggerModule &logger) {
  nlohmann::json::iterator count = object.find("count");
  bool validCount = false;
  if (count != object.end() && count->is_number_unsigned()) {
    validCount = count->get<unsigned long long>() >= 1 && count->get<unsigned long long>() <= static_cast<unsigned long long>(INT_MAX);
  } else if (count != object.end() && count->is_number_integer()) {
    validCount = count->get<long long>() >= 1 && count->get<long long>() <= INT_MAX;
  }
  if (!validCount) {
    logger.log(LogLevel::WARNING, "[Countly] addEvent, The count of the event [" + key + "] must be a whole number from 1 to " + std::to_string(INT_MAX) + ", so the default 1 is used.");
    logger.recordLogCode("w750");
    object["count"] = 1;
  }

  nlohmann::json::iterator duration = object.find("dur");
  if (duration != object.end() && (!duration->is_number() || duration->get<double>() < 0)) {
    logger.log(LogLevel::WARNING, "[Countly] addEvent, The duration of the event [" + key + "] can't be negative, so the default 0 is used.");
    logger.recordLogCode("w750");
    object.erase(duration);
  }
}

/**
 * Adds the stored events of the in-memory event queue to the array. An entry that is not valid JSON is dropped with
 * a warning instead of throwing out of the call that sends the events.
 */
void appendStoredEvents(const std::deque<std::string> &queue, nlohmann::json &events, LoggerModule &logger) {
  for (const std::string &event_json : queue) {
    nlohmann::json event = nlohmann::json::parse(event_json, nullptr, false);
    if (event.is_discarded()) {
      logger.log(LogLevel::WARNING, "[Countly] appendStoredEvents, A stored event is not valid JSON and is dropped.");
      continue;
    }
    events.push_back(event);
  }
}

/**
 * Whether the list of features contains the feature.
 */
bool containsFeature(const std::vector<std::string> &features, const char *feature) { return std::find(features.begin(), features.end(), feature) != features.end(); }

} // namespace


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
  // The app key of the request.
  std::string app_key;
};

/**
 * Body of a detached remote-config fetch. A free function rather than a Countly
 * member precisely so it cannot reach the instance.
 */
void runRemoteConfigFetch(std::shared_ptr<RemoteConfigFetch> fetch) {
  // Lower the process counter however this exits.
  struct Guard {
    ~Guard() { inflight_remote_config_fetches.fetch_sub(1); }
  } guard;

  try {
    while (true) {
      unsigned long long generation = 0;
      {
        std::lock_guard<std::mutex> lk(fetch->store->mutex);
        generation = fetch->store->user_generation;
      }

      const HTTPResponse response = fetch->requestModule->sendImmediate("/o/sdk", fetch->requestBuilder->buildRequest(fetch->data));

      std::lock_guard<std::mutex> lk(fetch->store->mutex);
      if (fetch->store->app_key != fetch->app_key) {
        // after an app key change the values, the flag and the pending download belong to the fetches for the new key
        return;
      }
      if (!response.success || !response.data.is_object()) {
        fetch->logger->log(LogLevel::DEBUG, "[Countly] runRemoteConfigFetch, the remote config download failed, the stored values stay.");
      } else if (fetch->store->user_generation != generation) {
        fetch->logger->log(LogLevel::DEBUG, "[Countly] runRemoteConfigFetch, the device ID changed during the download, so its values are discarded.");
      } else if (fetch->merge) {
        for (auto it = response.data.begin(); it != response.data.end(); ++it) {
          fetch->store->values[it.key()] = it.value();
        }
      } else {
        fetch->store->values = response.data;
      }

      if (!fetch->store->has_pending) {
        fetch->store->fetch_running.store(false);
        return;
      }
      fetch->data = fetch->store->pending_data;
      fetch->merge = fetch->store->pending_merge;
      fetch->store->has_pending = false;
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lk(fetch->store->mutex);
    if (fetch->store->app_key == fetch->app_key) {
      fetch->store->has_pending = false;
      fetch->store->fetch_running.store(false);
    }
    fetch->logger->log(LogLevel::DEBUG, std::string("[Countly] runRemoteConfigFetch, the remote config download failed: ") + e.what());
  }
}

/**
 * Whether an SDK behavior settings blacklist or whitelist lets 'key' through. An empty list lets every key through.
 */
bool isAllowedByFilter(const FilterList<std::set<std::string>> &filter, const std::string &key) {
  if (filter.filterList.empty()) {
    return true;
  }
  bool listed = filter.filterList.find(key) != filter.filterList.end();
  if (filter.isWhitelist) {
    return listed;
  }
  return !listed;
}
} // namespace

Countly::Countly() {
  {
    std::lock_guard<std::mutex> lk(networking_lifecycle_mutex);
    live_instance_count.fetch_add(1);
  }
  logger.reset(new cly::LoggerModule());
  health_check->setLogger(logger);
  logger->setHealthCheck(health_check);
  configuration.reset(new cly::CountlyConfiguration("", ""));
  // Built with the instance, so views() and crash() are usable before start(); their calls are ignored until then.
  crash_module.reset(new cly::CrashModule(configuration, logger, nullptr, mutex));
  views_module.reset(new cly::ViewsModule(this, logger));
  views_module->setConsentModule(consent_module);
  crash_module->setConsentModule(consent_module);
}

Countly::~Countly() {
  is_being_disposed = true;
  {
    std::lock_guard<std::mutex> change(consent_change_mutex);
  }
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
  if (requestQueueSize < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxRequestQueueSize, The request queue size must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  log(LogLevel::WARNING, "[Countly] alwaysUsePost, 'alwaysUsePost' is deprecated: every request is sent as POST, so the call has no effect.");
  logger->recordLogCode("w120");
  if (is_sdk_initialized) {
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->forcePost = value;
}

void Countly::setFailedRequestCooldown(int seconds) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setFailedRequestCooldown, This method can't be called after SDK initialization. Returning.");
    return;
  }
  if (seconds < 1) {
    log(LogLevel::WARNING, "[Countly] setFailedRequestCooldown, The cool-down must be at least 1 second, so the value [" + std::to_string(seconds) + "] is ignored.");
    logger->recordLogCode("w121");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->failedRequestCooldownSeconds = static_cast<unsigned int>(seconds);
}

void Countly::setRequestDropAgeHours(int hours) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setRequestDropAgeHours, This method can't be called after SDK initialization. Returning.");
    return;
  }
  if (hours < 0) {
    log(LogLevel::WARNING, "[Countly] setRequestDropAgeHours, The age can't be negative, so the value [" + std::to_string(hours) + "] is ignored.");
    logger->recordLogCode("w121");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->requestDropAgeHours = static_cast<unsigned int>(hours);
}

void Countly::disableBackoffMechanism() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] disableBackoffMechanism, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->backoffMechanismEnabled = false;
}

void Countly::disableGradualRequestCleaner() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] disableGradualRequestCleaner, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->gradualRequestCleanerEnabled = false;
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxKeyLength, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxValueSize, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxSegmentationValues, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxBreadcrumbCount, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxStackTraceLinesPerThread, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setMaxStackTraceLineLength, The limit must be at least 1, so the value [0] is ignored.");
    logger->recordLogCode("w121");
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
  if (is_sdk_initialized && !hasConsent(consent::USERS)) {
    log(LogLevel::DEBUG, "[Countly] setUserDetails, the 'users' consent is not given, the call is ignored.");
    return;
  }
  SDKLimits lim = configurationModule ? configurationModule->getLimits()
                                      : SDKLimits{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  std::map<std::string, std::string> limitedDetails;
  for (const auto &kv : value) {
    unsigned int cap = (kv.first == "picture") ? COUNTLY_MAX_VALUE_SIZE_PICTURE : lim.maxValueSize;
    limitedDetails[kv.first] = cly::limits::limitValue(kv.second, cap, logger.get(), "setUserDetails", kv.first);
  }
  nlohmann::json &details = session_params["user_details"];
  if (!details.is_object()) {
    details = nlohmann::json::object();
  }
  for (const auto &kv : limitedDetails) {
    details[kv.first] = kv.second;
  }

  if (!is_sdk_initialized) {
    log(LogLevel::INFO, "[Countly] setUserDetails, The SDK is not started yet, the details are sent with the first session.");
    return;
  }

  if (configuration->autoEventsOnUserProperties == true) {
    lk.unlock();
    flushEvents();
    lk.lock();
  }

  queueUserDetails();
}

void Countly::setCustomUserDetails(const std::map<std::string, std::string> &value) {
  // unique_lock so the mutex is released on scope exit, including on a throwing
  // json/map/addRequestToQueue op; unlock/re-acquire around the self-locking flushEvents().
  std::unique_lock<std::mutex> lk(*mutex);
  if (is_sdk_initialized && !hasConsent(consent::USERS)) {
    log(LogLevel::DEBUG, "[Countly] setCustomUserDetails, the 'users' consent is not given, the call is ignored.");
    return;
  }

  // Determine the post-filter custom property map first, then apply limits.
  std::map<std::string, std::string> customValue;
  SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
  if (configurationModule) {
    lim = configurationModule->getLimits();
    auto upFilter = configurationModule->getUserPropertyFilterList();
    for (const auto &kv : value) {
      if (isAllowedByFilter(upFilter, kv.first)) {
        customValue[kv.first] = kv.second;
      }
    }
    if (customValue.empty() && !upFilter.filterList.empty()) {
      log(LogLevel::DEBUG, "[Countly] setCustomUserDetails, All user properties were filtered out by SBS user property filter.");
      return;
    }
  } else {
    customValue = value;
  }

  // Apply SDK internal limits: truncate keys/values. No count cap for user
  // properties (that would be the out-of-scope 'upcl').
  std::map<std::string, std::string> limitedCustom;
  for (const auto &kv : customValue) {
    limitedCustom[cly::limits::limitKey(kv.first, lim.maxKeyLength, logger.get(), "setCustomUserDetails")] = cly::limits::limitValue(kv.second, lim.maxValueSize, logger.get(), "setCustomUserDetails", kv.first);
  }
  nlohmann::json &details = session_params["user_details"];
  if (!details.is_object()) {
    details = nlohmann::json::object();
  }
  nlohmann::json &custom = details["custom"];
  if (!custom.is_object()) {
    custom = nlohmann::json::object();
  }
  for (const auto &kv : limitedCustom) {
    custom[kv.first] = kv.second;
  }

  if (!is_sdk_initialized) {
    log(LogLevel::INFO, "[Countly] setCustomUserDetails, The SDK is not started yet, the properties are sent with the first session.");
    return;
  }

  if (configuration->autoEventsOnUserProperties == true) {
    lk.unlock();
    flushEvents();
    lk.lock();
  }

  queueUserDetails();
}

void Countly::multiplyCustomUserDetail(const std::string &key, double value) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] multiplyCustomUserDetail, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }
  if (!hasConsent(consent::USERS)) {
    log(LogLevel::DEBUG, "[Countly] multiplyCustomUserDetail, the 'users' consent is not given, the call is ignored.");
    return;
  }
  if (!std::isfinite(value)) {
    log(LogLevel::WARNING, "[Countly] multiplyCustomUserDetail, The parameter 'value' must be a finite number. Returning.");
    logger->recordLogCode("w700");
    return;
  }
  const std::string truncatedKey = cly::limits::limitKey(key, configurationModule->getLimits().maxKeyLength, logger.get(), "multiplyCustomUserDetail");
  if (truncatedKey.empty()) {
    log(LogLevel::WARNING, "[Countly] multiplyCustomUserDetail, The parameter 'key' can't be empty. Returning.");
    logger->recordLogCode("w700");
    return;
  }
  if (!isAllowedByFilter(configurationModule->getUserPropertyFilterList(), key)) {
    log(LogLevel::DEBUG, "[Countly] multiplyCustomUserDetail, User property filtered out by SBS user property filter: [" + key + "]");
    return;
  }

  if (configuration->autoEventsOnUserProperties == true) {
    flushEvents();
  }

  std::lock_guard<std::mutex> lk(*mutex);
  // Details set before start() and still waiting for the first session go first, so the server applies them before the multiplication.
  if (session_params.contains("user_details")) {
    queueUserDetails();
  }
  queueUserDetails({{"custom", {{truncatedKey, {{"$mul", value}}}}}});
}

void Countly::queueUserDetails() {
  if (hasConsent(consent::USERS)) {
    queueUserDetails(session_params["user_details"]);
  } else {
    log(LogLevel::DEBUG, "[Countly] queueUserDetails, the 'users' consent is not given, the waiting user details are dropped.");
  }
  session_params.erase("user_details");
}

void Countly::queueUserDetails(const nlohmann::json &userDetails) {
  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"user_details", userDetails.dump()}};
  requestModule->addRequestToQueue(data);
}

#pragma region User location

void Countly::setCountry(const std::string &country_code) {
  log(LogLevel::WARNING, "[Countly] setCountry, 'setCountry' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  logger->recordLogCode("w120");
  setLocation(country_code, "", "", "");
}

void Countly::setCity(const std::string &city_name) {
  log(LogLevel::WARNING, "[Countly] setCity, 'setCity' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  logger->recordLogCode("w120");
  setLocation("", city_name, "", "");
}

void Countly::setLocation(double lattitude, double longitude) {
  log(LogLevel::WARNING, "[Countly] setLocation, 'setLocation(latitude, longitude)' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  logger->recordLogCode("w120");

  std::ostringstream location_stream;
  location_stream << lattitude << ',' << longitude;
  setLocation("", "", location_stream.str(), "");
}

void Countly::setLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress) {
  log(LogLevel::INFO, "[Countly] setLocation, Setting location: countryCode = [" + countryCode + "], city = [" + city + "], gpsCoordinates = [" + gpsCoordinates + "], ipAddress = [" + ipAddress + "]");

  std::string validCountryCode = countryCode;
  if (!countryCode.empty() && !isCountryCode(countryCode)) {
    log(LogLevel::WARNING, "[Countly] setLocation, The country code [" + countryCode + "] is not an upper case ISO 3166-1 alpha-2 code such as 'US', so it is not used.");
    logger->recordLogCode("w700");
    validCountryCode.clear();
  }
  std::string validCoordinates = gpsCoordinates;
  if (!gpsCoordinates.empty() && !isGpsCoordinates(gpsCoordinates)) {
    log(LogLevel::WARNING, "[Countly] setLocation, The coordinates [" + gpsCoordinates + "] are not a latitude and a longitude in decimal degrees such as '52.52001,13.40495', so they are not used.");
    logger->recordLogCode("w700");
    validCoordinates.clear();
  }
  std::string validIpAddress = ipAddress;
  if (!ipAddress.empty() && !isIpAddress(ipAddress)) {
    log(LogLevel::WARNING, "[Countly] setLocation, The IP address [" + ipAddress + "] is not an IPv4 or IPv6 address, so it is not used.");
    logger->recordLogCode("w700");
    validIpAddress.clear();
  }
  if (validCountryCode.empty() != city.empty()) {
    log(LogLevel::WARNING, "[Countly] setLocation, It's required that both 'country_code' and 'city' should be set together");
  }
  const bool anyValue = !validCountryCode.empty() || !city.empty() || !validCoordinates.empty() || !validIpAddress.empty();

  if (!is_sdk_initialized) {
    std::lock_guard<std::mutex> lk(*mutex);
    session_params["city"] = city;
    session_params["ip_address"] = validIpAddress;
    session_params["location"] = validCoordinates;
    session_params["country_code"] = validCountryCode;
    log(LogLevel::DEBUG, "[Countly] setLocation, The SDK is not started yet, it uses the location from start() on.");
    return;
  }
  if (!hasConsent(consent::LOCATION)) {
    log(LogLevel::DEBUG, "[Countly] setLocation, the 'location' consent is not given, the call is ignored.");
    return;
  }
  if (configurationModule->getServerLocationTracking() == 0) {
    log(LogLevel::DEBUG, "[Countly] setLocation, The server's SDK behavior settings turned location tracking off, the call is ignored.");
    return;
  }

  bool sendNow = false;
  {
    // unique_lock so the mutex is released on scope exit (incl. throwing json writes)
    std::unique_lock<std::mutex> lk(*mutex);
    session_params["city"] = city;
    session_params["ip_address"] = validIpAddress;
    session_params["location"] = validCoordinates;
    session_params["country_code"] = validCountryCode;
    if (anyValue) {
      location_disabled_by_app = false;
    }
    // without a session start to come, the values go in a separate request right away
    sendNow = anyValue && (began_session || !hasConsent(consent::SESSIONS) || !configurationModule->isSessionTrackingEnabled());
  }

  if (sendNow) {
    _sendIndependantLocationRequest();
  }
}

void Countly::_sendIndependantLocationRequest() {
  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::DEBUG, "[Countly] _sendIndependantLocationRequest, Start");

  // the server uses an IP address only with a session start, so it goes with the next begin_session
  std::map<std::string, std::string> data;
  for (const char *key : {"city", "location", "country_code"}) {
    if (session_params.contains(key) && session_params[key].is_string() && !session_params[key].get<std::string>().empty()) {
      data[key] = session_params[key].get<std::string>();
    }
  }
  if (data.empty()) {
    return;
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  data["app_key"] = session_params["app_key"].get<std::string>();
  data["device_id"] = session_params["device_id"].get<std::string>();
  data["timestamp"] = std::to_string(timestamp.count());
  requestModule->addRequestToQueue(data);
}

void Countly::sendLocationErase() {
  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::DEBUG, "[Countly] sendLocationErase, erasing the location on the server");
  requestModule->addRequestToQueue({{"location", ""}});
}

#pragma endregion User location

#pragma region Device Id
void Countly::setDeviceID(const std::string &value) {
  bool generated = false;
  {
    std::lock_guard<std::mutex> lk(*mutex);
    generated = is_sdk_initialized && configuration->deviceIdType == DeviceIdType::SDK_GENERATED;
  }
  setDeviceID(value, generated);
}

void Countly::setDeviceID(const std::string &value, bool same_user) {
  if (value.empty()) {
    log(LogLevel::WARNING, "[Countly] setDeviceID, The device ID can't be empty, the call is ignored.");
    logger->recordLogCode("w601");
    return;
  }
  // unique_lock so the mutex is released on scope exit (incl. throwing json writes);
  // explicit unlock before the self-locking _changeDeviceId* helpers below.
  std::unique_lock<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] setDeviceID, Device ID change requested, new value = [" + value + "]");

  if (!is_sdk_initialized) {
    session_params["device_id"] = value;
    configuration->deviceId = value;
    log(LogLevel::DEBUG, "[Countly] setDeviceID, The SDK is not started yet, it uses this device ID from start() on.");
    return;
  }

  if (session_params["device_id"].get<std::string>() == value) {
    log(LogLevel::DEBUG, "[Countly] setDeviceID, New device id equals existing device id, ignoring.");
    return;
  }

  lk.unlock();
  if (same_user) {
    _changeDeviceIdWithMerge(value);
  } else {
    _changeDeviceIdWithoutMerge(value);
  }
}

bool Countly::resolveDeviceId(const std::string &given_device_id) {
  const nlohmann::json stored = nlohmann::json::parse(storageModule->getDeviceIdState(), nullptr, false);
  if (!stored.is_object() || !stored.contains("id") || !stored["id"].is_string() || stored["id"].get<std::string>().empty() || !stored.contains("type") || !stored["type"].is_number_integer()) {
    useNewDeviceId(given_device_id);
    return false;
  }

  configuration->deviceId = stored["id"].get<std::string>();
  configuration->deviceIdType = stored["type"].get<int>() == static_cast<int>(DeviceIdType::SDK_GENERATED) ? DeviceIdType::SDK_GENERATED : DeviceIdType::DEVELOPER_SUPPLIED;
  session_params["device_id"] = configuration->deviceId;
  if (configuration->clearStoredDeviceID) {
    log(LogLevel::INFO, "[Countly] start, The device ID stored by an earlier start is replaced, as asked with enableClearStoredDeviceID: [" + configuration->deviceId + "]");
    return true;
  }
  if (!given_device_id.empty() && given_device_id != configuration->deviceId) {
    log(LogLevel::WARNING, "[Countly] start, The device ID stored by an earlier start is used: [" + configuration->deviceId + "], and the device ID given with setDeviceID is ignored: [" + given_device_id +
                               "]. To use it, call enableClearStoredDeviceID() before start(), or change the device ID with setDeviceID after start().");
  } else {
    log(LogLevel::DEBUG, "[Countly] start, Using the device ID stored by an earlier start: [" + configuration->deviceId + "]");
  }
  return false;
}

void Countly::replaceStoredDeviceId(const std::string &given_device_id) {
  if (use_sqlite_storage) {
    nlohmann::json events = nlohmann::json::array();
    std::string event_ids;
    readStoredEvents(events, event_ids);
    sendEventsToRQ(events);
    if (!event_ids.empty()) {
      removeEventWithId(event_ids);
    }
  }
  useNewDeviceId(given_device_id);
}

void Countly::useNewDeviceId(const std::string &given_device_id) {
  if (!given_device_id.empty()) {
    configuration->deviceId = given_device_id;
    configuration->deviceIdType = DeviceIdType::DEVELOPER_SUPPLIED;
  } else {
    configuration->deviceId = cly::utils::generateUUID();
    configuration->deviceIdType = DeviceIdType::SDK_GENERATED;
    log(LogLevel::INFO, "[Countly] start, No device ID was given, so the SDK generated one: [" + configuration->deviceId + "]");
  }
  session_params["device_id"] = configuration->deviceId;
  storeDeviceId();
}

void Countly::enableClearStoredDeviceID() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] enableClearStoredDeviceID, This method can't be called after SDK initialization. Returning.");
    return;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  configuration->clearStoredDeviceID = true;
}

void Countly::storeDeviceId() {
  if (storageModule) {
    storageModule->storeDeviceIdState(nlohmann::json({{"id", configuration->deviceId}, {"type", static_cast<int>(configuration->deviceIdType)}}).dump());
  }
}

void Countly::setCustomCrashSegmentation(const std::map<std::string, std::string> &segmentation) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setCustomCrashSegmentation, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->crashSegmentation = segmentation;
}

void Countly::setCrashFilterCallback(CrashFilterCallback callback) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setCrashFilterCallback, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->crashFilter = callback;
}

void Countly::disableLocation() {
  log(LogLevel::INFO, "[Countly] disableLocation, turning location tracking off");
  location_disabled_by_app = true;
  clearStoredLocation();
  if (!is_sdk_initialized) {
    return;
  }
  if (configurationModule->getServerLocationTracking() == 1) {
    log(LogLevel::DEBUG, "[Countly] disableLocation, the server's SDK behavior settings keep location tracking on, so no erase request is sent.");
    return;
  }
  sendLocationErase();
}

nlohmann::json Countly::DeviceMetrics() { return deviceMetrics(); }

std::string Countly::CurrentViewId() { return views_module->currentViewId(); }

void Countly::LocationSettingChanged() {
  if (!start_finished) {
    return;
  }
  const int server = configurationModule->getServerLocationTracking();
  if (server == 0) {
    clearStoredLocation();
    if (hasConsent(consent::LOCATION)) {
      sendLocationErase();
    }
  } else if (server < 0 && location_disabled_by_app && hasConsent(consent::LOCATION)) {
    sendLocationErase();
  }
}

void Countly::AutomaticSessionTrackingChanged() {
  std::lock_guard<std::mutex> change(consent_change_mutex);
  if (!start_finished || is_being_disposed || stopping) {
    return;
  }
  if (isAutomaticSessionTracking()) {
    log(LogLevel::INFO, "[Countly] AutomaticSessionTrackingChanged, the server turned automatic session tracking on.");
    beginSessionInternal(true);
  } else {
    log(LogLevel::INFO, "[Countly] AutomaticSessionTrackingChanged, the server turned automatic session tracking off, the session the SDK runs ends.");
    endSessionInternal(true);
  }
}

bool Countly::isAutomaticSessionTracking() {
  const int server = configurationModule ? configurationModule->getAutomaticSessionTracking() : -1;
  return server >= 0 ? server == 1 : !configuration->manualSessionControl;
}

unsigned int Countly::serverPort(unsigned int url_port, int port, const char *caller) {
  if (url_port == 0) {
    return static_cast<unsigned int>(port);
  }
  if (port != 0 && static_cast<unsigned int>(port) != url_port) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", The port [" + std::to_string(port) + "] differs from the port in the server URL [" + std::to_string(url_port) + "], so the port in the URL is used.");
    logger->recordLogCode("w121");
  }
  return url_port;
}

std::string Countly::getDeviceID() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] getDeviceID, SDK is not initialized.");
    logger->recordLogCode("w101");
    return std::string();
  }
  std::lock_guard<std::mutex> lk(*mutex);
  return configuration->deviceId;
}

DeviceIdType Countly::getDeviceIDType() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] getDeviceIDType, SDK is not initialized.");
    logger->recordLogCode("w101");
    return DeviceIdType::DEVELOPER_SUPPLIED;
  }
  std::lock_guard<std::mutex> lk(*mutex);
  return configuration->deviceIdType;
}

/* Change device ID with merge after SDK has been initialized.*/
void Countly::_changeDeviceIdWithMerge(const std::string &value) {
  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::DEBUG, "[Countly] _changeDeviceIdWithMerge, deviceId = [" + value + "]");

  session_params["old_device_id"] = session_params["device_id"];
  session_params["device_id"] = value;
  configuration->deviceId = value;
  configuration->deviceIdType = DeviceIdType::DEVELOPER_SUPPLIED;
  storeDeviceId();
  clearRemoteConfigForNewUser();

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

/* Change device ID without merge after SDK has been initialized. Running views, events and user details that are still waiting are sent for the old device ID first, then all consent is removed without telling the server.*/
void Countly::_changeDeviceIdWithoutMerge(const std::string &value) {
  std::lock_guard<std::mutex> change(consent_change_mutex);
  log(LogLevel::DEBUG, "[Countly] _changeDeviceIdWithoutMerge, deviceId = [" + value + "]");

  views_module->stopRunningViews();
  // send all event to server and end current session of old user
  flushEvents();
  if (isAutomaticSessionTracking()) {
    endSessionInternal(true);
  }

  {
    std::lock_guard<std::mutex> lk(*mutex);
    if (session_params.contains("user_details")) {
      queueUserDetails();
    }
  }

  if (consent_module->isRequired()) {
    const std::vector<std::string> given = consent_module->featuresWithFlag(true);
    if (!given.empty()) {
      runConsentChange(given, false, ConsentChangeSource::DeviceIdChangedWithoutMerge);
    }
  }

  {
    std::lock_guard<std::mutex> lk(*mutex);
    session_params["device_id"] = value;
    configuration->deviceId = value;
    configuration->deviceIdType = DeviceIdType::DEVELOPER_SUPPLIED;
    storeDeviceId();
    previous_event_id.clear();
  }
  views_module->forgetViews();
  clearRemoteConfigForNewUser();

  // start a new session for new user
  if (isAutomaticSessionTracking()) {
    beginSessionInternal(true);
  }

  configurationModule->fetchConfigFromServer(nullptr);
  downloadRemoteConfigAutomatically();
}

void Countly::onMergeDelivered() {
  if (is_being_disposed || stopping) {
    return;
  }
  configurationModule->fetchConfigFromServer(nullptr);
  downloadRemoteConfigAutomatically();
}
#pragma endregion Device Id

bool Countly::isCallAllowed(const char *caller) {
  if (LoggerModule::isInsideCallback()) {
    return false;
  }
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", SDK is not initialized.");
    logger->recordLogCode("w101");
    return false;
  }
  return true;
}

void Countly::changeServerUrl(const std::string &host, int port) {
  if (!isCallAllowed("changeServerUrl")) {
    return;
  }
  std::string server_url;
  std::string server_path;
  unsigned int url_port = 0;
  bool trimmed_server_url = false;
  if (!parseServerUrl(host, server_url, url_port, server_path, trimmed_server_url)) {
    log(LogLevel::ERROR, "[Countly] changeServerUrl, The server URL [" + host + "] is not an absolute http:// or https:// URL, the current server URL stays.");
    logger->recordLogCode("e102");
    return;
  }
  if (port < 0 || port > 65535) {
    log(LogLevel::WARNING, "[Countly] changeServerUrl, Port number is out of valid boundaries. Setting it to 0.");
    logger->recordLogCode("w121");
    port = 0;
  }
  const unsigned int server_port = serverPort(url_port, port, "changeServerUrl");

  std::lock_guard<std::mutex> change(consent_change_mutex);
  if (is_being_disposed) {
    return;
  }
  log(LogLevel::INFO, "[Countly] changeServerUrl, Changing the server URL to [" + host + "], port = [" + std::to_string(server_port) + "]");
  requestModule->setServerUrl(server_url, server_port, server_path);
  if (consent_module->isRequired()) {
    queueConsentState();
  }
  fetchAfterServerChange(false);
}

void Countly::changeAppKey(const std::string &appKey) {
  if (!isCallAllowed("changeAppKey")) {
    return;
  }
  if (appKey.empty() || appKey == "YOUR_APP_KEY") {
    log(LogLevel::ERROR, "[Countly] changeAppKey, The app key can't be empty or the placeholder \"YOUR_APP_KEY\", the current app key stays.");
    logger->recordLogCode("e103");
    return;
  }

  std::lock_guard<std::mutex> change(consent_change_mutex);
  if (is_being_disposed) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(*mutex);
    if (configuration->appKey == appKey) {
      log(LogLevel::DEBUG, "[Countly] changeAppKey, The new app key is the current one, the call is ignored.");
      return;
    }
  }
  log(LogLevel::INFO, "[Countly] changeAppKey, Changing the app key to [" + appKey + "]");

  views_module->stopRunningViews();
  packEvents();
  bool sessionWasRunning = false;
  {
    // One section, so that the update loop can't begin a session with the old key after its session ended
    std::lock_guard<std::mutex> lk(*mutex);
    if (session_params.contains("user_details")) {
      queueUserDetails();
    }
    sessionWasRunning = began_session;
    if (began_session) {
      queueEndSession(true);
    }
    configuration->appKey = appKey;
    session_params["app_key"] = appKey;
    // In the same section, so that a fetch built with the new key always finds that key in the store
    std::lock_guard<std::mutex> store_lock(remote_config_store->mutex);
    remote_config_store->app_key = appKey;
    remote_config_store->values = nlohmann::json::object();
    remote_config_store->fetch_running.store(false);
    remote_config_store->has_pending = false;
  }
  if (sessionWasRunning) {
    views_module->resetFirstView();
    health_check->saveState();
  }

  if (consent_module->isRequired()) {
    queueConsentState();
  }
  if (sessionWasRunning) {
    beginSessionInternal(true);
  }
  fetchAfterServerChange(false);
}

void Countly::fetchAfterServerChange(bool remoteConfigRequested) {
  configurationModule->fetchConfigFromServer(nullptr);
  if (!remoteConfigRequested) {
    downloadRemoteConfigAutomatically();
  }
}

void Countly::flushQueues() {
  if (!isCallAllowed("flushQueues")) {
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] flushQueues, Clearing the request queue and the event queue.");
  requestModule->clearRequestQueue();
  if (use_sqlite_storage) {
    clearPersistentEQ();
  } else {
    event_queue.clear();
  }
}

void Countly::replaceAllAppKeysInQueueWithCurrentAppKey() {
  if (!isCallAllowed("replaceAllAppKeysInQueueWithCurrentAppKey")) {
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] replaceAllAppKeysInQueueWithCurrentAppKey, Giving every queued request the current app key.");
  requestModule->replaceAppKeysInQueue(configuration->appKey);
}

void Countly::removeDifferentAppKeysFromQueue() {
  if (!isCallAllowed("removeDifferentAppKeysFromQueue")) {
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  const long long removed = requestModule->removeOtherAppKeysFromQueue(configuration->appKey);
  log(LogLevel::INFO, "[Countly] removeDifferentAppKeysFromQueue, Removed [" + std::to_string(removed) + "] queued requests with another app key.");
}

void Countly::attemptToSendStoredRequests() {
  if (!isCallAllowed("attemptToSendStoredRequests")) {
    return;
  }

  log(LogLevel::INFO, "[Countly] attemptToSendStoredRequests, Sending the stored requests now.");
  packEvents();
  requestModule->processQueue(mutex);
}

void Countly::start(const std::string &app_key, const std::string &host, int port, bool start_thread) {
  // unique_lock so the mutex is released on scope exit (incl. allocation/json
  // throws while constructing modules); unlock/re-acquire around the self-locking
  // configurationModule->fetch*/beginSession() calls below.
  std::unique_lock<std::mutex> lk(*mutex);
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] start, SDK has already been initialized, 'start' should not be called a second time!");
    logger->recordLogCode("w110");
    return;
  }

  if (app_key.empty() || app_key == "YOUR_APP_KEY") {
    log(LogLevel::ERROR, "[Countly] start, The app key must be the key of your Countly app, not empty and not the placeholder 'YOUR_APP_KEY'. SDK will not be initialized.");
    return;
  }

  std::string server_url;
  std::string server_path;
  unsigned int url_port = 0;
  bool trimmed_server_url = false;
  if (!parseServerUrl(host, server_url, url_port, server_path, trimmed_server_url)) {
    log(LogLevel::ERROR, "[Countly] start, The server URL [" + host + "] is not an absolute http or https URL, for example 'https://your.server.ly'. SDK will not be initialized.");
    return;
  }
  if (trimmed_server_url) {
    log(LogLevel::WARNING, "[Countly] start, The trailing '/' characters of the server URL are removed: [" + server_url + server_path + "]");
  }

#ifdef COUNTLY_USE_SQLITE
  if (configuration->databasePath == "" || configuration->databasePath == " ") {
    log(LogLevel::WARNING, "[Countly] start, No database path is set, so events and requests are kept in memory and are lost when the app closes. Call setDatabasePath() before start() to keep them.");
  } else {
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
    use_sqlite_storage = true;
  }
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

  (void)start_thread;

  if (port < 0 || port > 65535) {
    log(LogLevel::WARNING, "[Countly] start, Port number is out of valid boundaries. Setting it to 0.");
    logger->recordLogCode("w121");
    port = 0;
  }

  configuration->port = serverPort(url_port, port, "start");
  configuration->appKey = app_key;
  configuration->serverUrl = server_url;
  configuration->serverPath = server_path;

  session_params["app_key"] = app_key;
  {
    std::lock_guard<std::mutex> store_lock(remote_config_store->mutex);
    remote_config_store->app_key = app_key;
  }

  if (use_sqlite_storage) {
    storageModule.reset(new StorageModuleDB(configuration, logger));
    storageModule->init();
    if (!storageModule->isInitialized() || !createEventTableSchema()) {
      log(LogLevel::ERROR, "[Countly] start, Could not open the database at path: '" + configuration->databasePath + "', so events and requests are kept in memory and are lost when the app closes. Please verify the path is valid and writable.");
      releaseDatabasePathClaim();
      use_sqlite_storage = false;
    }
  }
  if (!use_sqlite_storage) {
    storageModule.reset(new StorageModuleMemory(configuration, logger));
    storageModule->init();
  }
  const std::string given_device_id = session_params.contains("device_id") ? session_params["device_id"].get<std::string>() : std::string();
  const bool replace_stored_device_id = resolveDeviceId(given_device_id);

  requestBuilder.reset(new RequestBuilder(configuration, logger));
  requestModule.reset(new RequestModule(configuration, logger, requestBuilder, storageModule));
  configurationModule.reset(new cly::ConfigurationModule(this, configuration, logger, requestBuilder, storageModule, requestModule, mutex));
  crash_module->setRequestModule(requestModule);
  crash_module->setDelegates(this);

  requestModule->setConfigurationProvider(configurationModule);
  requestModule->setOnMergeDelivered([this]() { onMergeDelivered(); });
  views_module->setConfigurationProvider(configurationModule);
  crash_module->setConfigurationProvider(configurationModule);
  health_check->setStorage(storageModule);
  if (replace_stored_device_id) {
    replaceStoredDeviceId(given_device_id);
  }

  is_sdk_initialized = true; // after this point SDK is initialized.

  lk.unlock();
  configurationModule->fetchConfigFromStorage();
  configurationModule->fetchConfigFromServer(session_params);
  configurationModule->startServerConfigUpdateTimer(session_params);
  activateConsent();
  lk.lock();

  const bool automatic_sessions = isAutomaticSessionTracking();
  if (!running) {

    if (automatic_sessions) {
      lk.unlock();
      beginSessionInternal(true);
      lk.lock();
    }

    stop_thread = false;

    try {
      thread.reset(new std::thread(&Countly::updateLoop, this));
    } catch (const std::system_error &e) {
      std::ostringstream log_message;
      log_message << "[Countly] start, Could not create thread: " << e.what();
      log(LogLevel::FATAL, log_message.str());
    }
  }

  lk.unlock();
  // without a session start to carry the empty location, a separate request keeps the location off
  if (!isLocationOn() && (!hasConsent(consent::SESSIONS) || !configurationModule->isSessionTrackingEnabled())) {
    sendLocationErase();
  }
  start_finished = true;
  if (isAutomaticSessionTracking() != automatic_sessions) {
    // the server's automatic session tracking setting arrived while start() ran
    AutomaticSessionTrackingChanged();
  }
  downloadRemoteConfigAutomatically();
}

/**
 * startOnCloud is deprecated and this is going to be removed in the future.
 */
void Countly::startOnCloud(const std::string &app_key) {
  log(LogLevel::WARNING, "[Countly] startOnCloud, 'startOnCloud' is deprecated, this is going to be removed in the future.");
  logger->recordLogCode("w120");
  this->start(app_key, "https://cloud.count.ly", 443);
}

bool Countly::startRemoteConfigFetch(const std::map<std::string, std::string> &data, bool merge, const char *caller, bool queueIfRunning) {
  // Checked here rather than on the fetch thread: see RemoteConfigFetch on why it
  // must not hold the configuration module.
  if (configurationModule->isNetworkingEnabled() == false) {
    log(LogLevel::DEBUG, std::string("[Countly] ") + caller + ", the SDK behavior settings turned networking off, so remote config is not downloaded.");
    return false;
  }

  std::string app_key;
  {
    std::lock_guard<std::mutex> lk(remote_config_store->mutex);
    if (remote_config_store->fetch_running.load()) {
      if (queueIfRunning) {
        remote_config_store->has_pending = true;
        remote_config_store->pending_data = data;
        remote_config_store->pending_merge = merge;
        log(LogLevel::DEBUG, std::string("[Countly] ") + caller + ", a remote config fetch is in flight; this one follows it.");
        return true;
      }
      log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", a remote config fetch is already in flight; ignoring this call.");
      return false;
    }
    remote_config_store->fetch_running.store(true);
    // taken with the flag, so that the fetch owns the flag exactly while its app key is the current one
    app_key = remote_config_store->app_key;
  }

  std::shared_ptr<RemoteConfigFetch> fetch(new RemoteConfigFetch());
  fetch->requestModule = requestModule;
  fetch->requestBuilder = requestBuilder;
  fetch->logger = logger;
  fetch->store = remote_config_store;
  fetch->data = data;
  fetch->merge = merge;
  fetch->app_key = app_key;

  inflight_remote_config_fetches.fetch_add(1);
  try {
    // Detached on purpose: nothing may wait for an HTTP round trip, least of all
    // ~Countly. The body holds `fetch` and therefore everything it touches.
    std::thread(runRemoteConfigFetch, fetch).detach();
  } catch (const std::system_error &e) {
    inflight_remote_config_fetches.fetch_sub(1);
    {
      std::lock_guard<std::mutex> lk(remote_config_store->mutex);
      remote_config_store->fetch_running.store(false);
    }
    log(LogLevel::ERROR, std::string("[Countly] ") + caller + ", could not create remote config thread: " + e.what());
    return false;
  }
  return true;
}

std::map<std::string, std::string> Countly::remoteConfigRequest() {
  std::map<std::string, std::string> data = {{"method", "rc"}};
  if (hasConsent(consent::METRICS)) {
    data["metrics"] = deviceMetrics().dump();
  }
  if (configuration->enrollABOnRCDownload) {
    data["oi"] = "1";
  }
  return data;
}

void Countly::downloadRemoteConfigAutomatically() {
  bool automatic = false;
  {
    std::lock_guard<std::mutex> lk(*mutex);
    automatic = remote_config_enabled;
  }
  if (!automatic || !hasConsent(consent::REMOTE_CONFIG)) {
    return;
  }
  startRemoteConfigFetch(remoteConfigRequest(), false, "downloadRemoteConfigAutomatically", true);
}

void Countly::clearRemoteConfigForNewUser() {
  std::lock_guard<std::mutex> lk(remote_config_store->mutex);
  remote_config_store->values = nlohmann::json::object();
  remote_config_store->user_generation++;
}

void Countly::stop() {
  // Joining the SDK Behavior Settings timer or an in-flight remote-config fetch
  // here would make stop() block where it previously returned at once, and
  // integrators call it on the UI thread at shutdown (see examples/qt_demo). Both
  // joins belong in ~Countly, which is where the lifetime problem they solve
  // actually arises.
  stopping = true;
  if (is_sdk_initialized && !is_being_disposed) {
    // queued while the update loop still runs, so that its last pass can send them
    packEvents();
    if (isAutomaticSessionTracking()) {
      endSessionInternal(true);
    }
  }
  _deleteThread();
  health_check->saveState();
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
  std::string eventKey = event.getKey();
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] addEvent, SDK is not initialized.");
    if (hasEventConsent(eventKey)) {
      logger->recordLogCode("w101");
    }
    return;
  }

  if (eventKey.empty()) {
    log(LogLevel::WARNING, "[Countly] addEvent, The event key can't be empty, the event is not recorded.");
    logger->recordLogCode("w701");
    return;
  }

  if (!hasEventConsent(eventKey)) {
    log(LogLevel::DEBUG, "[Countly] addEvent, the consent for the event [" + eventKey + "] is not given, it is not recorded.");
    return;
  }

  const bool isInternalEvent = isInternalEventKey(eventKey);
  if (!isInternalEvent) {
    logger->recordUsage(HEALTH_CHECK_AREA_EVENTS, "record");
  }

  if (!configurationModule->isTrackingEnabled()) {
    log(LogLevel::WARNING, "[Countly] addEvent, The SDK behavior settings turned tracking off, so the event [" + eventKey + "] is not recorded.");
    logger->recordLogCode("w505");
    return;
  }

  // Check custom event tracking (only blocks custom events)
  if (!isInternalEvent && !configurationModule->isCustomEventTrackingEnabled()) {
    log(LogLevel::DEBUG, "[Countly] addEvent, custom event tracking is disabled in server configuration, can not add event with key: [" + eventKey + "]");
    return;
  }

  // Apply event filter (only for custom events)
  if (!isInternalEvent && !isAllowedByFilter(configurationModule->getEventFilterList(), eventKey)) {
    log(LogLevel::DEBUG, "[Countly] addEvent, event filtered out by SBS event filter: [" + eventKey + "]");
    return;
  }

  // Copy the event so we can apply segmentation filters without modifying the caller's object
  cly::Event filteredEvent = event;
  validateEventValues(filteredEvent.object, eventKey, *logger);

  // Apply segmentation filters (only for custom events)
  if (!isInternalEvent && filteredEvent.hasSegmentation()) {
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

  // Custom events follow every limit. The SDK's own keys and values in internal events are not cut, but the values
  // an app gives in them follow the value size limit.
  SDKLimits limits = configurationModule->getLimits();
  nlohmann::json &object = filteredEvent.object;
  nlohmann::json::iterator segmentation = object.find("segmentation");
  if (!isInternalEvent) {
    object["key"] = cly::limits::limitKey(eventKey, limits.maxKeyLength, logger.get(), "addEvent");
    if (segmentation != object.end() && segmentation->is_object()) {
      cly::limits::limitSegmentation(*segmentation, limits, logger.get(), "addEvent");
    }
  } else if (segmentation != object.end() && segmentation->is_object()) {
    for (nlohmann::json::iterator it = segmentation->begin(); it != segmentation->end(); ++it) {
      const bool viewField = eventKey == "[CLY]_view" && (it.key() == "name" || it.key() == "_idv" || it.key() == "segment");
      if (it.value().is_string() && !viewField) {
        it.value() = cly::limits::limitValue(it.value().get<std::string>(), limits.maxValueSize, logger.get(), "addEvent", it.key());
      }
    }
  }

  const std::string currentViewId = views_module->currentViewId();
  std::unique_lock<std::mutex> lk(*mutex);
  if (!object.contains("id")) {
    object["id"] = cly::utils::generateEventID();
  }
  if (eventKey == "[CLY]_view") {
    if (!object.contains("pvid")) {
      object["pvid"] = currentViewId;
    }
  } else {
    object["cvid"] = currentViewId;
    if (!isInternalEvent) {
      object["peid"] = previous_event_id;
      previous_event_id = object["id"].get<std::string>();
    }
  }

  if (use_sqlite_storage) {
    addEventToSqlite(filteredEvent);
  } else {
    event_queue.push_back(filteredEvent.serialize());
  }
  lk.unlock();
  if (isImmediateFlushEventKey(eventKey)) {
    packEvents();
  } else {
    checkAndSendEventToRQ();
  }
}

bool Countly::IsSessionRunning() {
  std::lock_guard<std::mutex> lk(*mutex);
  return began_session;
}

std::string Countly::PlatformName() {
  nlohmann::json metrics = deviceMetrics();
  nlohmann::json::const_iterator os = metrics.find("_os");
  return os != metrics.end() && os->is_string() ? os->get<std::string>() : std::string();
}

nlohmann::json Countly::deviceMetrics() {
  nlohmann::json metrics = nlohmann::json::object();
  metrics["_os"] = cly::platform::osName();
  const std::string osVersion = cly::platform::osVersion();
  if (!osVersion.empty()) {
    metrics["_os_version"] = osVersion;
  }
  if (configuration->metrics.is_object()) {
    for (nlohmann::json::const_iterator it = configuration->metrics.begin(); it != configuration->metrics.end(); ++it) {
      metrics[it.key()] = it.value();
    }
  }
  return metrics;
}

void Countly::checkAndSendEventToRQ() {
  nlohmann::json events = nlohmann::json::array();
  int queueSize = checkEQSize();
  // if queue size could not be get return early
  if (queueSize < 0) {
    return;
  }
  // unique_lock so the mutex is released on scope exit, including when
  // sendEventsToRQ throws; unlock/re-acquire around the self-locking
  // fillEventsIntoJson().
  std::unique_lock<std::mutex> lk(*mutex);
  const int threshold = static_cast<int>(std::min(configurationModule->getEventQueueSizeLimit(), static_cast<unsigned int>(COUNTLY_MAX_EVENT_QUEUE_SIZE)));
  if (use_sqlite_storage) {
    if (queueSize >= threshold) {
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
  } else {
    if (queueSize >= threshold) {
      log(LogLevel::DEBUG, "[Countly] checkAndSendEventToRQ, Event queue threshold is reached, sending the events to the request queue");
      appendStoredEvents(event_queue, events, *logger);
      sendEventsToRQ(events);
      event_queue.clear();
    }
  }
}

void Countly::setMaxEvents(size_t value) {
  log(LogLevel::WARNING, "[Countly] setMaxEvents, 'setMaxEvents' and 'SetMaxEventsPerMessage' are deprecated. Use 'setEventsToRQThreshold' instead.");
  logger->recordLogCode("w120");
  setEventsToRQThreshold(static_cast<int>(value));
}

void Countly::setEventsToRQThreshold(int value) {
  log(LogLevel::DEBUG, "[Countly] setEventsToRQThreshold, Given threshold:[" + std::to_string(value) + "]");
  std::unique_lock<std::mutex> lk(*mutex);
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly] setEventsToRQThreshold, The threshold must be at least 1, so the value [" + std::to_string(value) + "] is ignored.");
    logger->recordLogCode("w121");
    return;
  }

  // set the value
  configuration->eventQueueThreshold = value;
  // if current queue size is greater than the new threshold, send events to RQ
  lk.unlock();
  checkAndSendEventToRQ();
}

void Countly::flushEvents(std::chrono::seconds timeout) {
  (void)timeout;
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] flushEvents, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }
  log(LogLevel::DEBUG, "[Countly] flushEvents, moving the waiting events into a request");
  packEvents();
}

bool Countly::attemptSessionUpdateEQ() {
  // return false if event queue is empty
  if (use_sqlite_storage) {
    int event_count = checkEQSize();
    if (event_count <= 0) {
      return false;
    }
  } else {
    std::lock_guard<std::mutex> lk(*mutex);
    if (event_queue.empty()) {
      return false;
    }
  }
  if (isAutomaticSessionTracking()) {
    return !updateSessionInternal(true);
  } else {
    packEvents();
    return false;
  }
}

void Countly::clearEQInternal() {
  if (use_sqlite_storage) {
    clearPersistentEQ();
  } else {
    // event_queue is guarded by mutex; the only caller (flushEvents) holds no lock here.
    std::lock_guard<std::mutex> lk(*mutex);
    event_queue.clear();
  }
}

#ifdef COUNTLY_BUILD_TESTS
std::vector<std::string> Countly::debugReturnStateOfEQ() {
  try {

#ifdef COUNTLY_USE_SQLITE
    if (use_sqlite_storage) {
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
      return v;
    }
#endif
    std::lock_guard<std::mutex> lk(*mutex);
    std::vector<std::string> v(event_queue.begin(), event_queue.end());
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
  if (use_sqlite_storage) {
    log(LogLevel::WARNING, "[Countly] debugInjectRawEvent, not supported with SQLite storage.");
  } else {
    event_queue.push_back(raw);
  }
}
#endif

bool Countly::beginSession() {
  if (rejectSessionCallInAutomaticMode("beginSession", "begin")) {
    return false;
  }
  return beginSessionInternal(false);
}

bool Countly::rejectSessionCallInAutomaticMode(const char *caller, const char *usage) {
  if (!is_sdk_initialized || !isAutomaticSessionTracking()) {
    return false;
  }
  if (hasConsent(consent::SESSIONS)) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, usage);
    logger->recordLogCode("w740");
  }
  log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", The SDK tracks sessions automatically, so the call is ignored. To make session calls, call enableManualSessionControl() before start().");
  return true;
}

bool Countly::beginSessionInternal(bool automatic) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] beginSession, SDK is not initialized.");
    if (hasConsent(consent::SESSIONS)) {
      logger->recordLogCode("w101");
    }
    return false;
  }
  if (!hasConsent(consent::SESSIONS)) {
    log(LogLevel::DEBUG, "[Countly] beginSession, the 'sessions' consent is not given, no session begins.");
    return false;
  }
  if (!automatic) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "begin");
  }
  // unique_lock so the mutex is always released on scope exit, including the
  // early returns below and any exception (e.g. a json type_error from a
  // session_params access, or addRequestToQueue) thrown while it is held.
  std::unique_lock<std::mutex> lk(*mutex);
  log(LogLevel::INFO, "[Countly] beginSession, Starting session");
  if (configurationModule->isSessionTrackingEnabled() == false) {
    log(LogLevel::DEBUG, "[Countly] beginSession, Session tracking is disabled in server configuration, can not begin session.");
    return false;
  }
  if (automatic && stopping) {
    log(LogLevel::DEBUG, "[Countly] beginSession, The SDK is stopping, no session begins.");
    return false;
  }
  if (began_session == true) {
    if (automatic) {
      log(LogLevel::DEBUG, "[Countly] beginSession, Session is already active.");
    } else {
      log(LogLevel::WARNING, "[Countly] beginSession, Session is already active, the call is ignored.");
      logger->recordLogCode("w760");
    }
    return true;
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::map<std::string, std::string> data = {{"timestamp", std::to_string(timestamp.count())}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"begin_session", "1"}};

  if (isLocationOn()) {
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
  } else {
    data["location"] = "";
  }

  if (session_params.contains("user_details")) {
    if (hasConsent(consent::USERS)) {
      data["user_details"] = session_params["user_details"].dump();
    } else {
      log(LogLevel::DEBUG, "[Countly] beginSession, the 'users' consent is not given, the waiting user details are dropped.");
    }
    session_params.erase("user_details");
  }

  if (hasConsent(consent::METRICS)) {
    data["metrics"] = deviceMetrics().dump();
  }

  requestModule->addRequestToQueue(data);
  session_params.erase("user_details");
  last_sent_session_request = std::chrono::steady_clock::now();
  began_session = true;
  if (automatic) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "begin:a");
  }
  lk.unlock();
  return true;
}

/**
 * @brief Update session
 */
bool Countly::updateSession() {
  if (rejectSessionCallInAutomaticMode("updateSession", "update")) {
    return false;
  }
  return updateSessionInternal(false);
}

bool Countly::updateSessionInternal(bool automatic) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateSession, SDK is not initialized.");
    if (hasConsent(consent::SESSIONS)) {
      logger->recordLogCode("w101");
    }
    return false;
  }
  if (!hasConsent(consent::SESSIONS)) {
    if (automatic) {
      packEvents();
      return true;
    }
    log(LogLevel::DEBUG, "[Countly] updateSession, the 'sessions' consent is not given, the session is not updated.");
    return false;
  }
  if (!automatic) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "update");
  }
  // unique_lock so the mutex is always released on scope exit, including when an
  // exception propagates out of a call made while the lock is held.
  std::unique_lock<std::mutex> lk(*mutex, std::defer_lock);
  try {
    // Check if there was a session, if not try to start one
    lk.lock();
    if (configurationModule->isSessionTrackingEnabled() == false) {
      lk.unlock();
      if (automatic) {
        // sessions are off, but the update timer still sends the events
        packEvents();
        return true;
      }
      log(LogLevel::DEBUG, "[Countly] updateSession, Session tracking is disabled in server configuration, can not update session.");
      return false;
    }
    if (began_session == false) {
      lk.unlock();
      if (!automatic) {
        log(LogLevel::WARNING, "[Countly] updateSession, SDK is in manual session control mode and there is no active session. Please start a session first.");
        logger->recordLogCode("w760");
        return false;
      }
      if (!beginSessionInternal(true)) {
        log(LogLevel::DEBUG, "[Countly] updateSession, Failed to begin session.");
        packEvents();
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
      if (use_sqlite_storage) {
        lk.unlock();
        fillEventsIntoJson(events, event_ids);
        lk.lock();
      } else {
        appendStoredEvents(event_queue, events, *logger);
      }
    } else {
      log(LogLevel::DEBUG, "[Countly] updateSession, EQ empty.");
    }
    lk.unlock();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
    lk.lock();

    // the update timer reports the session once an update interval passed; the app's own call always does
    if (!automatic || duration.count() >= configurationModule->getSessionUpdateInterval()) {
      log(LogLevel::DEBUG, "[Countly] updateSession, Sending session update.");
      std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}};
      requestModule->addRequestToQueue(data);
      if (automatic) {
        logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "update:a");
      }

      last_sent_session_request += duration;
    }

    // report events if there are any to request queue
    if (!no_events) {
      sendEventsToRQ(events);
    }

// clear event queue
// TODO: check if we want to totally wipe the event queue in memory but not in database
    if (use_sqlite_storage) {
      if (!event_ids.empty()) {
        // this is a partial clearance, we only remove the events that were sent
        removeEventWithId(event_ids);
      }
    } else {
      event_queue.clear();
    }
  } catch (const std::exception &e) {
    std::ostringstream log_message;
    log_message << "[Countly] updateSession, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
    logger->recordLogCode("e901");
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
      if (use_sqlite_storage) {
        lk.unlock();
        fillEventsIntoJson(events, event_ids);
        lk.lock();
      } else {
        appendStoredEvents(event_queue, events, *logger);
      }
    } else {
      log(LogLevel::DEBUG, "[Countly] packEvents, EQ empty.");
    }
    // report events if there are any to request queue
    if (!no_events) {
      sendEventsToRQ(events);
    }

// clear event queue
// TODO: check if we want to totally wipe the event queue in memory but not in database
    if (use_sqlite_storage) {
      if (!event_ids.empty()) {
        // this is a partial clearance, we only remove the events that were sent
        removeEventWithId(event_ids);
      }
    } else {
      event_queue.clear();
    }
  } catch (const std::exception &e) {
    std::ostringstream log_message;
    log_message << "[Countly] packEvents, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
    logger->recordLogCode("e901");
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
  if (rejectSessionCallInAutomaticMode("endSession", "end")) {
    return false;
  }
  return endSessionInternal(false);
}

bool Countly::endSessionInternal(bool automatic) {
  if (!is_sdk_initialized && !is_being_disposed) {
    log(LogLevel::WARNING, "[Countly] endSession, SDK is not initialized.");
    if (hasConsent(consent::SESSIONS)) {
      logger->recordLogCode("w101");
    }
    return false;
  }
  if (!hasConsent(consent::SESSIONS)) {
    log(LogLevel::DEBUG, "[Countly] endSession, the 'sessions' consent is not given, there is no session to end.");
    return false;
  }
  if (!automatic) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "end");
  }
  log(LogLevel::INFO, "[Countly] endSession, Ending session");
  bool sessionRunning = false;
  {
    std::lock_guard<std::mutex> lk(*mutex);
    sessionRunning = began_session;
  }
  if (!sessionRunning && !is_being_disposed && configurationModule->isSessionTrackingEnabled() == false) {
    log(LogLevel::DEBUG, "[Countly] endSession, Session tracking is disabled in server configuration, there is no session to end.");
    return false;
  }
  // the events recorded during the session reach the server before its end
  if (sessionRunning && !is_being_disposed) {
    packEvents();
  }

  // lock_guard so the mutex is released on scope exit, including the early
  // returns below and any exception (e.g. a json type_error from a session_params
  // access, or addRequestToQueue) thrown while it is held.
  //
  // The began_session check belongs inside this lock, together with the write
  // that clears it: read outside, two concurrent calls could both see an active
  // session and both queue an end_session request. stop() on one thread and a
  // device id change on another is enough to get there.
  std::unique_lock<std::mutex> lk(*mutex);
  if (began_session == false) {
    if (automatic || is_being_disposed) {
      log(LogLevel::DEBUG, "[Countly] endSession, There is no active session to end.");
    } else {
      log(LogLevel::WARNING, "[Countly] endSession, There is no active session to end, the call is ignored.");
      logger->recordLogCode("w760");
    }
    return true;
  }

  if (is_being_disposed) {
    // if SDK is being destroyed, don't attempt to send the end-session request.
    return false;
  }

  queueEndSession(automatic);
  lk.unlock();
  views_module->resetFirstView();
  health_check->saveState();
  return true;
}

void Countly::queueEndSession(bool automatic) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(Countly::getTimestamp().time_since_epoch());
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  // getSessionDuration() takes this same mutex, so compute it inline here.
  const auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_sent_session_request);

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}, {"timestamp", std::to_string(timestamp.count())}, {"end_session", "1"}};
  requestModule->addRequestToQueue(data);

  last_sent_session_request = now;
  began_session = false;
  if (automatic) {
    logger->recordUsage(HEALTH_CHECK_AREA_SESSIONS, "end:a");
  }
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

  event_count = use_sqlite_storage ? checkPersistentEQSize() : checkMemoryEQSize();
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
  // lock_guard so the mutex is released on every exit path, including an
  // exception thrown while it is held.
  std::lock_guard<std::mutex> lk(*mutex);
  readStoredEvents(events, event_ids);
}

void Countly::readStoredEvents(nlohmann::json &events, std::string &event_ids) {
  event_ids = "";
  if (database_path.empty()) {
    log(LogLevel::FATAL, "[Countly] fillEventsIntoJson, SQLite database path is not set.");
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
        const char *stored_event = table[(event_index * column_count) + 1];
        nlohmann::json event = nlohmann::json::parse(stored_event ? stored_event : "", nullptr, false);
        if (event.is_discarded()) {
          log(LogLevel::WARNING, "[Countly] fillEventsIntoJson, A stored event is not valid JSON and is dropped.");
          continue;
        }
        events.push_back(event);
      }

      log(LogLevel::DEBUG, "[Countly] fillEventsIntoJson, Events count = [" + std::to_string(events.size()) + "]");

      if (row_count > 0) {
        event_id_stream.seekp(-1, event_id_stream.cur);
        event_id_stream << ')';

        // write event ids to a string stream (or more like copy out that stream here) to be used in the delete statement
        event_ids = event_id_stream.str();
      }
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

    return_value = cly::utils::openDatabase(database_path, &database);
    if (return_value == SQLITE_OK) {
      const std::string serialized_event = event.serialize();
      sqlite3_stmt *statement = nullptr;
      return_value = sqlite3_prepare_v2(database, "INSERT INTO events (event) VALUES(?);", -1, &statement, nullptr);
      if (return_value == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, serialized_event.c_str(), -1, SQLITE_TRANSIENT);
        return_value = sqlite3_step(statement);
      }
      if (return_value != SQLITE_DONE) {
        // Say which operation failed: this event is being dropped, and a bare
        // "database is locked" gives no hint of what was lost.
        log(LogLevel::ERROR, "[Countly] addEventToSqlite, Event was not stored, SQLite error: " + std::string(sqlite3_errmsg(database)));
        logger->recordLogCode("e501");
      }
      sqlite3_finalize(statement);
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
#else
// Builds without SQLite never choose SQLite storage, so these are never called
int Countly::checkPersistentEQSize() { return -1; }

void Countly::clearPersistentEQ() {}

void Countly::removeEventWithId(const std::string &event_ids) { (void)event_ids; }

void Countly::fillEventsIntoJson(nlohmann::json &events, std::string &event_ids) {
  (void)events;
  (void)event_ids;
}

void Countly::readStoredEvents(nlohmann::json &events, std::string &event_ids) {
  (void)events;
  (void)event_ids;
}

void Countly::addEventToSqlite(const cly::Event &event) { (void)event; }

bool Countly::createEventTableSchema() { return false; }

void Countly::setDatabasePath(const std::string &path) {
  (void)path;
  log(LogLevel::WARNING, "[Countly] setDatabasePath, This build does not include SQLite storage (COUNTLY_USE_SQLITE), so the path is ignored and events and requests are kept in memory.");
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

std::chrono::steady_clock::duration Countly::getSessionDuration(std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lk(*mutex);
  std::chrono::steady_clock::duration duration = now - last_sent_session_request;
  return duration;
}

std::chrono::steady_clock::duration Countly::getSessionDuration() { return Countly::getSessionDuration(std::chrono::steady_clock::now()); }

void Countly::updateLoop() {
  log(LogLevel::DEBUG, "[Countly][updateLoop]");
  {
    std::lock_guard<std::mutex> lk(*mutex);
    running = true;
  }
  try {
    sendHealthCheck();
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    while (true) {
      if (configuration->immediateRequestOnStop) {
        std::unique_lock<std::mutex> lk(*mutex);
        stop_cv.wait_for(lk, std::chrono::milliseconds(wait_milliseconds), [this] {
          return stop_thread.load();
        });
        if (stop_thread) {
          stop_thread = false;
          running = false;
          return;
        }
      } else {
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
      }
      updateTick(last_update);
    }
    std::lock_guard<std::mutex> lk(*mutex);
    running = false;
  } catch (const std::exception &e) {
    running = false;
    log(LogLevel::ERROR, std::string("[Countly][updateLoop] exception in update loop: ") + e.what());
  } catch (...) {
    running = false;
    log(LogLevel::FATAL, "[Countly][updateLoop] unknown non-std::exception caught, stopping update loop");
  }
}

void Countly::updateTick(std::chrono::steady_clock::time_point &last_update) {
  if (!stopping) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_update >= std::chrono::seconds(configurationModule->getSessionUpdateInterval())) {
      last_update = now;
      if (isAutomaticSessionTracking()) {
        updateSessionInternal(true);
      } else {
        packEvents();
      }
    }
  }
  // after stop() this is the last pass, which closes by sending
  requestModule->processQueue(mutex, stopping);
  saveHealthCheckOnTimer();
}

void Countly::disableHealthCheck() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] disableHealthCheck, You can not disable the health check after SDK initialization.");
    return;
  }

  configuration->healthCheckEnabled = false;
}

void Countly::sendHealthCheck() {
  if (!configuration->healthCheckEnabled) {
    log(LogLevel::DEBUG, "[Countly] sendHealthCheck, the health check is disabled, nothing is sent");
    return;
  }

  if (configurationModule == nullptr || !configurationModule->isNetworkingEnabled()) {
    log(LogLevel::DEBUG, "[Countly] sendHealthCheck, networking is disabled by the SDK behavior settings, the counters wait for the next start");
    return;
  }

  if (health_check_sent.exchange(true)) {
    log(LogLevel::DEBUG, "[Countly] sendHealthCheck, the health check was already sent");
    return;
  }

  try {
    const nlohmann::json snapshot = health_check->snapshot();
    std::map<std::string, std::string> data = {{"hc", snapshot.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)}};

    nlohmann::json::const_iterator appVersion = configuration->metrics.find("_app_version");
    if (hasConsent(consent::METRICS) && appVersion != configuration->metrics.end() && appVersion->is_string() && !appVersion->get<std::string>().empty()) {
      data["metrics"] = nlohmann::json({{"_app_version", appVersion->get<std::string>()}}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string request;
    {
      // buildRequest reads the app key and the device ID, which other threads change under the instance mutex
      std::lock_guard<std::mutex> lk(*mutex);
      request = requestBuilder->buildRequest(data);
    }
    log(LogLevel::DEBUG, "[Countly] sendHealthCheck, sending the health check");
    const HTTPResponse response = requestModule->sendImmediate("/i", request);
    if (response.success && response.data.is_object() && response.data.contains("result")) {
      log(LogLevel::DEBUG, "[Countly] sendHealthCheck, the server accepted the health check");
      health_check->clearSentValues(snapshot);
    } else {
      log(LogLevel::DEBUG, "[Countly] sendHealthCheck, the server did not accept the health check, the counters are kept for the next start");
    }
  } catch (const std::exception &e) {
    log(LogLevel::DEBUG, std::string("[Countly] sendHealthCheck, the health check could not be sent: ") + e.what());
  }
}

void Countly::saveHealthCheckOnTimer() {
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (configurationModule != nullptr && now - last_health_check_save >= std::chrono::seconds(configurationModule->getSessionUpdateInterval())) {
    last_health_check_save = now;
    health_check->saveState();
  }
}

void Countly::enableRemoteConfig() {
  std::lock_guard<std::mutex> lk(*mutex);
  remote_config_enabled = true;
}

void Countly::enrollABOnRCDownload() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] enrollABOnRCDownload, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  configuration->enrollABOnRCDownload = true;
}

void Countly::updateRemoteConfig() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateRemoteConfig, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }
  if (!hasConsent(consent::REMOTE_CONFIG)) {
    log(LogLevel::DEBUG, "[Countly] updateRemoteConfig, the 'remote-config' consent is not given, nothing is downloaded.");
    return;
  }

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Replaces the stored values wholesale, since this fetches all of them.
  startRemoteConfigFetch(remoteConfigRequest(), false, "updateRemoteConfig");
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
    logger->recordLogCode("w101");
    return;
  }
  if (!hasConsent(consent::REMOTE_CONFIG)) {
    log(LogLevel::DEBUG, "[Countly] updateRemoteConfigFor, the 'remote-config' consent is not given, nothing is downloaded.");
    return;
  }
  std::map<std::string, std::string> data = remoteConfigRequest();
  nlohmann::json keys_json = nlohmann::json::array();
  for (size_t key_index = 0; keys != nullptr && key_index < key_count; key_index++) {
    if (keys[key_index].empty()) {
      log(LogLevel::WARNING, "[Countly] updateRemoteConfigFor, An empty key is ignored.");
      logger->recordLogCode("w700");
      continue;
    }
    keys_json.push_back(keys[key_index]);
  }
  if (keys_json.empty()) {
    // without a valid key this is a download of every value
    startRemoteConfigFetch(data, false, "updateRemoteConfigFor");
    return;
  }
  data["keys"] = keys_json.dump();

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Merges into the stored values, since this fetches only some of them.
  startRemoteConfigFetch(data, true, "updateRemoteConfigFor");
}

void Countly::updateRemoteConfigExcept(std::string *keys, size_t key_count) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] updateRemoteConfigExcept, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }
  if (!hasConsent(consent::REMOTE_CONFIG)) {
    log(LogLevel::DEBUG, "[Countly] updateRemoteConfigExcept, the 'remote-config' consent is not given, nothing is downloaded.");
    return;
  }
  std::map<std::string, std::string> data = remoteConfigRequest();
  nlohmann::json keys_json = nlohmann::json::array();
  for (size_t key_index = 0; keys != nullptr && key_index < key_count; key_index++) {
    if (keys[key_index].empty()) {
      log(LogLevel::WARNING, "[Countly] updateRemoteConfigExcept, An empty key is ignored.");
      logger->recordLogCode("w700");
      continue;
    }
    keys_json.push_back(keys[key_index]);
  }
  if (keys_json.empty()) {
    // without a valid key this is a download of every value
    startRemoteConfigFetch(data, false, "updateRemoteConfigExcept");
    return;
  }
  data["omit_keys"] = keys_json.dump();

  // Asynchronous and detached: neither this call nor destruction waits for it.
  // Merges into the stored values, since this fetches only some of them.
  startRemoteConfigFetch(data, true, "updateRemoteConfigExcept");
}

void Countly::setRequiresConsent(bool required) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setRequiresConsent, This method can't be called after SDK initialization. Returning.");
    return;
  }

  consent_module->setRequiredByApp(required);
}

void Countly::setConsentEnabled(const std::vector<std::string> &featureNames) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] setConsentEnabled, This method can't be called after SDK initialization. Returning.");
    return;
  }

  std::vector<std::string> features;
  for (const std::string &name : featureNames) {
    if (!ConsentModule::isFeature(name)) {
      log(LogLevel::WARNING, "[Countly] setConsentEnabled, [" + name + "] is not a feature this SDK offers, it is ignored.");
      logger->recordLogCode("w402");
      continue;
    }
    features.push_back(name);
  }
  consent_module->setOnlyGiven(features);
}

void Countly::giveConsent(const std::vector<std::string> &featureNames) { changeConsent(featureNames, true, "giveConsent"); }

void Countly::removeConsent(const std::vector<std::string> &featureNames) { changeConsent(featureNames, false, "removeConsent"); }

void Countly::giveConsentAll() { changeConsent(consent::allFeatures(), true, "giveConsentAll"); }

void Countly::removeConsentAll() { changeConsent(consent::allFeatures(), false, "removeConsentAll"); }

void Countly::setConsent(const std::vector<std::string> &featureNames, bool isConsentGiven) { changeConsent(featureNames, isConsentGiven, "setConsent"); }

bool Countly::getConsent(const std::string &featureName) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] getConsent, SDK is not initialized.");
    logger->recordLogCode("w101");
    return false;
  }
  if (!consent_module->isRequired()) {
    return true;
  }
  if (!consent_module->isKnownName(featureName)) {
    log(LogLevel::WARNING, "[Countly] getConsent, [" + featureName + "] is neither a feature this SDK offers nor a feature group.");
    logger->recordLogCode("w402");
    return false;
  }
  return consent_module->getConsent(featureName);
}

void Countly::createFeatureGroup(const std::string &groupName, const std::vector<std::string> &featureNames) {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] createFeatureGroup, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }
  if (groupName.empty()) {
    log(LogLevel::WARNING, "[Countly] createFeatureGroup, The parameter 'groupName' can't be empty. Returning.");
    logger->recordLogCode("w700");
    return;
  }
  if (ConsentModule::isFeature(groupName)) {
    log(LogLevel::WARNING, "[Countly] createFeatureGroup, a group can't take the name of the feature [" + groupName + "]. Returning.");
    logger->recordLogCode("w700");
    return;
  }
  if (featureNames.empty()) {
    log(LogLevel::WARNING, "[Countly] createFeatureGroup, no feature was given for the group [" + groupName + "]. Returning.");
    logger->recordLogCode("w402");
    return;
  }

  std::vector<std::string> features;
  for (const std::string &name : featureNames) {
    if (!ConsentModule::isFeature(name)) {
      log(LogLevel::WARNING, "[Countly] createFeatureGroup, [" + name + "] is not a feature this SDK offers, it is ignored.");
      logger->recordLogCode("w402");
      continue;
    }
    features.push_back(name);
  }
  if (features.empty()) {
    log(LogLevel::DEBUG, "[Countly] createFeatureGroup, the group [" + groupName + "] has no feature this SDK offers, it is not created.");
    return;
  }
  consent_module->createGroup(groupName, features);
}

void Countly::setConsentFeatureGroup(const std::string &groupName, bool isConsentGiven) { changeConsent({groupName}, isConsentGiven, "setConsentFeatureGroup"); }

void Countly::checkAllConsent() {
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly] checkAllConsent, SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }

  std::ostringstream state;
  state << "[Countly] checkAllConsent, consent is " << (consent_module->isRequired() ? "required" : "not required");
  for (const std::string &feature : consent::allFeatures()) {
    state << ", " << feature << ": " << (consent_module->getConsent(feature) ? "given" : "not given");
  }
  log(LogLevel::INFO, state.str());
}

void Countly::ConsentRequirementChanged() {
  std::lock_guard<std::mutex> change(consent_change_mutex);
  if (is_being_disposed || configurationModule == nullptr) {
    return;
  }

  const int requiredByServer = configurationModule->getConsentRequired();
  if (!consent_active || !consent_module->requirementChangesWith(requiredByServer)) {
    consent_module->setRequiredByServer(requiredByServer);
    return;
  }

  const std::vector<std::string> withoutConsent = consent_module->featuresWithFlag(false);
  if (consent_module->isRequired()) {
    log(LogLevel::INFO, "[Countly] ConsentRequirementChanged, the server lifted the consent requirement.");
    consent_module->setRequiredByServer(requiredByServer);
    consentChanged(withoutConsent, true, ConsentChangeSource::ServerRequirement);
    return;
  }

  log(LogLevel::INFO, "[Countly] ConsentRequirementChanged, the server requires consent.");
  consentWillBeRemoved(withoutConsent);
  consent_module->setRequiredByServer(requiredByServer);
  queueConsentState();
  consentChanged(withoutConsent, false, ConsentChangeSource::ServerRequirement);
}

bool Countly::hasConsent(const char *feature) const { return consent_module->getConsent(feature); }

bool Countly::hasEventConsent(const std::string &key) const {
  if (key == "[CLY]_view") {
    return hasConsent(consent::VIEWS);
  }
  if (key == "[CLY]_nps" || key == "[CLY]_survey") {
    return hasConsent(consent::FEEDBACK);
  }
  if (key == "[CLY]_star_rating") {
    return hasConsent(consent::STAR_RATING) || hasConsent(consent::FEEDBACK);
  }
  if (key == "[CLY]_orientation") {
    return hasConsent(consent::USERS);
  }
  if (key == "[CLY]_push_action") {
    return hasConsent("push");
  }
  if (key == "[CLY]_action") {
    return hasConsent("clicks") || hasConsent("scrolls");
  }
  if (key == "[CLY]_content_shown" || key == "[CLY]_content_interacted") {
    return hasConsent("content");
  }
  return hasConsent(consent::EVENTS);
}

bool Countly::isLocationOn() {
  if (!hasConsent(consent::LOCATION)) {
    return false;
  }
  const int server = configurationModule->getServerLocationTracking();
  return server >= 0 ? server == 1 : !location_disabled_by_app;
}

void Countly::changeConsent(const std::vector<std::string> &names, bool given, const char *caller) {
  if (LoggerModule::isInsideCallback()) {
    return;
  }
  if (!is_sdk_initialized) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", SDK is not initialized.");
    logger->recordLogCode("w101");
    return;
  }

  std::lock_guard<std::mutex> change(consent_change_mutex);
  if (is_being_disposed) {
    return;
  }
  if (!consent_module->isRequired()) {
    log(LogLevel::INFO, std::string("[Countly] ") + caller + ", consent is not required, the call is ignored.");
    return;
  }
  if (names.empty()) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", no feature was given, the call is ignored.");
    logger->recordLogCode("w402");
    return;
  }

  std::vector<std::string> unknown;
  const std::vector<std::string> features = consent_module->expand(names, unknown);
  for (const std::string &name : unknown) {
    log(LogLevel::WARNING, std::string("[Countly] ") + caller + ", [" + name + "] is neither a feature this SDK offers nor a feature group, it is ignored.");
    logger->recordLogCode("w402");
  }

  const std::vector<std::string> changing = consent_module->featuresToChange(features, given);
  if (changing.empty()) {
    log(LogLevel::DEBUG, std::string("[Countly] ") + caller + ", the consent of the given features does not change.");
    return;
  }
  runConsentChange(changing, given, ConsentChangeSource::App);
}

void Countly::runConsentChange(const std::vector<std::string> &features, bool given, ConsentChangeSource source) {
  log(LogLevel::DEBUG, std::string("[Countly] runConsentChange, ") + (given ? "giving" : "removing") + " consent for " + std::to_string(features.size()) + " feature(s)");
  if (!given) {
    consentWillBeRemoved(features);
  }
  consent_module->setGiven(features, given);
  if (source != ConsentChangeSource::DeviceIdChangedWithoutMerge && consent_module->isRequired()) {
    queueConsentState();
  }
  consentChanged(features, given, source);
}

void Countly::consentWillBeRemoved(const std::vector<std::string> &features) {
  if (containsFeature(features, consent::VIEWS)) {
    views_module->stopRunningViews();
  }
  if (containsFeature(features, consent::SESSIONS)) {
    endSessionInternal(true);
  }
}

void Countly::consentChanged(const std::vector<std::string> &features, bool given, ConsentChangeSource source) {
  if (!given) {
    if (containsFeature(features, consent::CRASHES)) {
      crash_module->forgetBreadcrumbs();
    }
    if (containsFeature(features, consent::USERS)) {
      std::lock_guard<std::mutex> lk(*mutex);
      session_params.erase("user_details");
    }
    if (containsFeature(features, consent::LOCATION)) {
      clearStoredLocation();
      if (source != ConsentChangeSource::DeviceIdChangedWithoutMerge) {
        sendLocationErase();
      }
    }
    return;
  }

  bool sessionBegan = false;
  if (containsFeature(features, consent::SESSIONS) && isAutomaticSessionTracking()) {
    bool sessionRunning = false;
    {
      std::lock_guard<std::mutex> lk(*mutex);
      sessionRunning = began_session;
    }
    sessionBegan = !sessionRunning && beginSessionInternal(true);
  }
  if (containsFeature(features, consent::LOCATION) && !sessionBegan && isLocationOn()) {
    bool sendNow = false;
    {
      std::lock_guard<std::mutex> lk(*mutex);
      sendNow = began_session || !hasConsent(consent::SESSIONS) || !configurationModule->isSessionTrackingEnabled();
    }
    if (sendNow) {
      _sendIndependantLocationRequest();
    }
  }
  if (containsFeature(features, consent::REMOTE_CONFIG)) {
    downloadRemoteConfigAutomatically();
  }
}

void Countly::queueConsentState() {
  const std::string state = consent_module->state().dump();
  log(LogLevel::DEBUG, "[Countly] queueConsentState, consent = [" + state + "]");
  std::lock_guard<std::mutex> lk(*mutex);
  requestModule->addRequestToQueue({{"consent", state}});
}

void Countly::clearStoredLocation() {
  std::lock_guard<std::mutex> lk(*mutex);
  session_params.erase("city");
  session_params.erase("location");
  session_params.erase("country_code");
  session_params.erase("ip_address");
}

void Countly::activateConsent() {
  std::lock_guard<std::mutex> change(consent_change_mutex);
  consent_module->setRequiredByServer(configurationModule->getConsentRequired());
  consent_active = true;
  if (!consent_module->isRequired()) {
    return;
  }

  queueConsentState();
}
} // namespace cly
