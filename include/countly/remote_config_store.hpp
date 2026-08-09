#ifndef COUNTLY_REMOTE_CONFIG_STORE_HPP_
#define COUNTLY_REMOTE_CONFIG_STORE_HPP_

#include "nlohmann/json.hpp"
#include <atomic>
#include <mutex>

namespace cly {

/**
 * The remote config values, plus the flag that keeps two fetches from running at
 * once.
 *
 * This lives in its own reference-counted object rather than inside Countly
 * because the fetch runs on a detached thread: destroying an instance must not
 * wait for an HTTP round trip, so the thread can outlive the instance. It holds a
 * shared_ptr to this store (and to the modules it uses), which is what makes
 * writing the result after the owner is gone harmless instead of a
 * use-after-free.
 *
 * Guarded by its own mutex, not the instance mutex, for the same reason: the
 * instance may no longer exist.
 */
struct RemoteConfigStore {
  std::mutex mutex;
  nlohmann::json values = nlohmann::json::object();

  // True from the moment a fetch is started until its thread body returns.
  std::atomic<bool> fetch_running{false};
};

} // namespace cly
#endif
