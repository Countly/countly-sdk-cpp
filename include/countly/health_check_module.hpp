#ifndef HEALTH_CHECK_MODULE_HPP_
#define HEALTH_CHECK_MODULE_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

namespace cly {
class LoggerModule;
class StorageModuleBase;

/**
 * The counters of the SDK health check: warnings and errors logged, the last request the server
 * refused, backoffs, method usage ("fu") and log codes ("lc"), kept since the last health check
 * the server accepted. Logs at debug level only, as every warning and error the SDK logs is
 * counted here.
 */
class HealthCheckModule {
public:
  static const int COUNT_CAP = 65535;
  static const size_t MAX_ERROR_MESSAGE_LENGTH = 1000;

  HealthCheckModule();
  ~HealthCheckModule();

  /**
   * Sets the logger the module writes its debug lines to. Held weakly, as the logger holds this module.
   */
  void setLogger(std::weak_ptr<LoggerModule> logger);

  /**
   * Sets the storage the counters are kept in and adds the counters a previous run stored to the
   * current ones. A stored state that is not valid JSON is cleared and reported as log code e503.
   */
  void setStorage(std::shared_ptr<StorageModuleBase> storage);

  /** Counts one warning the SDK logged. */
  void logWarning();

  /** Counts one error the SDK logged. */
  void logError();

  /**
   * Keeps the answer of the server to a queued request it did not accept. Only the most recent one
   * is kept. A status code outside 1..999 is ignored, and the body is cut to 1000 characters.
   */
  void logFailedNetworkRequest(long statusCode, const std::string &errorResponse);

  /**
   * Counts a backoff of the request queue and extends the current run of backoffs in a row. "cbom"
   * holds the longest run right away. Records log code w320, and w321 when the backoff continues a run.
   */
  void logBackoffRequest();

  /** Ends the current run of backoffs in a row, when a queued request was answered in time. */
  void logConsecutiveBackoffRequest();

  /**
   * Counts one call of a method for the "fu" map. A call the SDK made by itself carries ":a", a
   * deprecated method ":d". Empty names are ignored, and every count stops at 65535.
   */
  void recordUsage(const std::string &area, const std::string &method);

  /** Counts occurrences of a log code for the "lc" map, a level letter and three digits such as "w740". */
  void recordLogCode(const std::string &code, int count = 1);

  /**
   * The health check parameter as it is sent. "fu" and "lc" are always present, empty when nothing
   * was counted, so the server can tell an SDK that reports them from one that does not.
   */
  nlohmann::json snapshot();

  /**
   * Takes away what an accepted health check reported, so what was counted while it was on its way
   * is reported next time. The last failed request and the longest backoff run are cleared only
   * when they did not change since. Stores what is left.
   */
  void clearSentValues(const nlohmann::json &sent);

  /** Stores the counters, when a storage is set. */
  void saveState();

  /** Resets every counter to its default and clears the stored state. */
  void clearAndSave();

private:
  void setupInitialCounters(const std::string &initialState);
  void addUsage(const std::string &area, const std::string &method, long long count);
  void addLogCode(const std::string &code, long long count);
  nlohmann::json toJson(bool forStorage);
  void debugLog(const std::string &message);

  std::mutex _mutex;
  std::weak_ptr<LoggerModule> _logger;
  std::shared_ptr<StorageModuleBase> _storage;

  long long _countLogWarning = 0;
  long long _countLogError = 0;
  long _statusCode = -1;
  std::string _errorMessage;
  long long _countBackoffRequest = 0;
  long long _consecutiveBackoffRequest = 0;
  long long _consecutiveBackoffRequestCounter = 0;
  std::map<std::string, std::map<std::string, long long>> _methodUsage;
  std::map<std::string, long long> _logCodes;
};
} // namespace cly
#endif
