#include "countly/health_check_module.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_module_base.hpp"

#include <algorithm>

namespace cly {
namespace {
const char KEY_LOG_ERROR[] = "LErr";
const char KEY_LOG_WARNING[] = "LWar";
const char KEY_STATUS_CODE[] = "RStatC";
const char KEY_ERROR_MESSAGE[] = "REMsg";
const char KEY_BACKOFF_REQUEST[] = "BReq";
const char KEY_CONSECUTIVE_BACKOFF_REQUEST[] = "CBReq";
const char KEY_METHOD_USAGE[] = "MU";
const char KEY_LOG_CODES[] = "LC";

const char REQUEST_KEY_ERROR_COUNT[] = "el";
const char REQUEST_KEY_WARNING_COUNT[] = "wl";
const char REQUEST_KEY_STATUS_CODE[] = "sc";
const char REQUEST_KEY_ERROR_MESSAGE[] = "em";
const char REQUEST_KEY_BACKOFF_REQUEST[] = "bom";
const char REQUEST_KEY_CONSECUTIVE_BACKOFF_REQUEST[] = "cbom";
const char REQUEST_KEY_METHOD_USAGE[] = "fu";
const char REQUEST_KEY_LOG_CODES[] = "lc";

/**
 * A non-negative whole number stored under 'key', or 0 when the entry is missing or of another type.
 */
long long storedCount(const nlohmann::json &stored, const char *key) {
  nlohmann::json::const_iterator it = stored.find(key);
  if (it == stored.end() || !it->is_number()) {
    return 0;
  }
  return std::max(0LL, it->get<long long>());
}

/**
 * The number reported under 'key' in a sent snapshot, or 'fallback' when it is missing.
 */
long long sentNumber(const nlohmann::json &sent, const char *key, long long fallback) {
  nlohmann::json::const_iterator it = sent.find(key);
  if (it == sent.end() || !it->is_number()) {
    return fallback;
  }
  return it->get<long long>();
}

/**
 * Takes a reported amount away from a count, removing the count when nothing is left.
 */
void subtract(std::map<std::string, long long> &counts, const std::string &key, long long reported) {
  std::map<std::string, long long>::iterator it = counts.find(key);
  if (it == counts.end()) {
    return;
  }
  it->second -= reported;
  if (it->second <= 0) {
    counts.erase(it);
  }
}
} // namespace

const int HealthCheckModule::COUNT_CAP;
const size_t HealthCheckModule::MAX_ERROR_MESSAGE_LENGTH;

HealthCheckModule::HealthCheckModule() {}

HealthCheckModule::~HealthCheckModule() {}

void HealthCheckModule::setLogger(std::weak_ptr<LoggerModule> logger) {
  std::lock_guard<std::mutex> lk(_mutex);
  _logger = logger;
}

void HealthCheckModule::debugLog(const std::string &message) {
  std::shared_ptr<LoggerModule> logger;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    logger = _logger.lock();
  }
  if (logger) {
    logger->log(LogLevel::DEBUG, "[Countly] [HealthCheckModule] " + message);
  }
}

void HealthCheckModule::setStorage(std::shared_ptr<StorageModuleBase> storage) {
  {
    std::lock_guard<std::mutex> lk(_mutex);
    _storage = storage;
  }
  // read outside the lock: the storage logs, and a warning it logs is counted here
  std::string stored = storage ? storage->getHealthCheckState() : std::string();
  setupInitialCounters(stored);
}

void HealthCheckModule::setupInitialCounters(const std::string &initialState) {
  if (initialState.empty()) {
    return;
  }

  const nlohmann::json stored = nlohmann::json::parse(initialState, nullptr, false);
  if (stored.is_discarded() || !stored.is_object()) {
    clearAndSave();
    {
      std::lock_guard<std::mutex> lk(_mutex);
      addLogCode("e503", 1);
    }
    debugLog("setupInitialCounters, the stored health check state could not be read and is reset");
    return;
  }

  {
    std::lock_guard<std::mutex> lk(_mutex);
    _countLogWarning += storedCount(stored, KEY_LOG_WARNING);
    _countLogError += storedCount(stored, KEY_LOG_ERROR);
    nlohmann::json::const_iterator statusCode = stored.find(KEY_STATUS_CODE);
    if (_statusCode == -1 && statusCode != stored.end() && statusCode->is_number()) {
      _statusCode = statusCode->get<long>();
    }
    nlohmann::json::const_iterator errorMessage = stored.find(KEY_ERROR_MESSAGE);
    if (_errorMessage.empty() && errorMessage != stored.end() && errorMessage->is_string()) {
      _errorMessage = errorMessage->get<std::string>();
    }
    _countBackoffRequest += storedCount(stored, KEY_BACKOFF_REQUEST);
    _consecutiveBackoffRequest = std::max(_consecutiveBackoffRequest, storedCount(stored, KEY_CONSECUTIVE_BACKOFF_REQUEST));

    nlohmann::json::const_iterator usage = stored.find(KEY_METHOD_USAGE);
    if (usage != stored.end() && usage->is_object()) {
      for (nlohmann::json::const_iterator area = usage->begin(); area != usage->end(); ++area) {
        if (!area->is_object()) {
          continue;
        }
        for (nlohmann::json::const_iterator method = area->begin(); method != area->end(); ++method) {
          if (method->is_number()) {
            addUsage(area.key(), method.key(), method->get<long long>());
          }
        }
      }
    }

    nlohmann::json::const_iterator codes = stored.find(KEY_LOG_CODES);
    if (codes != stored.end() && codes->is_object()) {
      for (nlohmann::json::const_iterator code = codes->begin(); code != codes->end(); ++code) {
        if (code->is_number()) {
          addLogCode(code.key(), code->get<long long>());
        }
      }
    }
  }

  debugLog("setupInitialCounters, loaded the stored health check state");
}

void HealthCheckModule::logWarning() {
  std::lock_guard<std::mutex> lk(_mutex);
  _countLogWarning++;
}

void HealthCheckModule::logError() {
  std::lock_guard<std::mutex> lk(_mutex);
  _countLogError++;
}

void HealthCheckModule::logFailedNetworkRequest(long statusCode, const std::string &errorResponse) {
  if (statusCode <= 0 || statusCode >= 1000) {
    return;
  }

  std::lock_guard<std::mutex> lk(_mutex);
  _statusCode = statusCode;
  _errorMessage = errorResponse.size() > MAX_ERROR_MESSAGE_LENGTH ? errorResponse.substr(0, MAX_ERROR_MESSAGE_LENGTH) : errorResponse;
}

void HealthCheckModule::logBackoffRequest() {
  std::lock_guard<std::mutex> lk(_mutex);
  bool continuesRun = _consecutiveBackoffRequestCounter > 0;
  _countBackoffRequest++;
  _consecutiveBackoffRequestCounter++;
  _consecutiveBackoffRequest = std::max(_consecutiveBackoffRequest, _consecutiveBackoffRequestCounter);
  addLogCode("w320", 1);
  if (continuesRun) {
    addLogCode("w321", 1);
  }
}

void HealthCheckModule::logConsecutiveBackoffRequest() {
  std::lock_guard<std::mutex> lk(_mutex);
  _consecutiveBackoffRequestCounter = 0;
}

void HealthCheckModule::recordUsage(const std::string &area, const std::string &method) {
  if (area.empty() || method.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(_mutex);
  addUsage(area, method, 1);
}

void HealthCheckModule::recordLogCode(const std::string &code, int count) {
  if (code.empty() || count <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lk(_mutex);
  addLogCode(code, count);
}

void HealthCheckModule::addUsage(const std::string &area, const std::string &method, long long count) {
  if (count <= 0) {
    return;
  }

  long long &current = _methodUsage[area][method];
  current = std::min(current + count, static_cast<long long>(COUNT_CAP));
}

void HealthCheckModule::addLogCode(const std::string &code, long long count) {
  if (count <= 0) {
    return;
  }

  long long &current = _logCodes[code];
  current = std::min(current + count, static_cast<long long>(COUNT_CAP));
}

nlohmann::json HealthCheckModule::toJson(bool forStorage) {
  nlohmann::json usage = nlohmann::json::object();
  for (std::map<std::string, std::map<std::string, long long>>::const_iterator area = _methodUsage.begin(); area != _methodUsage.end(); ++area) {
    nlohmann::json methods = nlohmann::json::object();
    for (std::map<std::string, long long>::const_iterator method = area->second.begin(); method != area->second.end(); ++method) {
      methods[method->first] = method->second;
    }
    usage[area->first] = methods;
  }

  nlohmann::json codes = nlohmann::json::object();
  for (std::map<std::string, long long>::const_iterator code = _logCodes.begin(); code != _logCodes.end(); ++code) {
    codes[code->first] = code->second;
  }

  nlohmann::json json = nlohmann::json::object();
  json[forStorage ? KEY_LOG_ERROR : REQUEST_KEY_ERROR_COUNT] = _countLogError;
  json[forStorage ? KEY_LOG_WARNING : REQUEST_KEY_WARNING_COUNT] = _countLogWarning;
  json[forStorage ? KEY_STATUS_CODE : REQUEST_KEY_STATUS_CODE] = _statusCode;
  json[forStorage ? KEY_ERROR_MESSAGE : REQUEST_KEY_ERROR_MESSAGE] = _errorMessage;
  json[forStorage ? KEY_BACKOFF_REQUEST : REQUEST_KEY_BACKOFF_REQUEST] = _countBackoffRequest;
  json[forStorage ? KEY_CONSECUTIVE_BACKOFF_REQUEST : REQUEST_KEY_CONSECUTIVE_BACKOFF_REQUEST] = _consecutiveBackoffRequest;
  json[forStorage ? KEY_METHOD_USAGE : REQUEST_KEY_METHOD_USAGE] = usage;
  json[forStorage ? KEY_LOG_CODES : REQUEST_KEY_LOG_CODES] = codes;
  return json;
}

nlohmann::json HealthCheckModule::snapshot() {
  std::lock_guard<std::mutex> lk(_mutex);
  return toJson(false);
}

void HealthCheckModule::clearSentValues(const nlohmann::json &sent) {
  {
    std::lock_guard<std::mutex> lk(_mutex);
    _countLogError = std::max(0LL, _countLogError - sentNumber(sent, REQUEST_KEY_ERROR_COUNT, 0));
    _countLogWarning = std::max(0LL, _countLogWarning - sentNumber(sent, REQUEST_KEY_WARNING_COUNT, 0));
    _countBackoffRequest = std::max(0LL, _countBackoffRequest - sentNumber(sent, REQUEST_KEY_BACKOFF_REQUEST, 0));

    if (_consecutiveBackoffRequest == sentNumber(sent, REQUEST_KEY_CONSECUTIVE_BACKOFF_REQUEST, -1)) {
      _consecutiveBackoffRequest = 0;
    }

    nlohmann::json::const_iterator sentMessage = sent.find(REQUEST_KEY_ERROR_MESSAGE);
    std::string reportedMessage = (sentMessage != sent.end() && sentMessage->is_string()) ? sentMessage->get<std::string>() : std::string();
    if (_statusCode == sentNumber(sent, REQUEST_KEY_STATUS_CODE, -2) && _errorMessage == reportedMessage) {
      _statusCode = -1;
      _errorMessage.clear();
    }

    nlohmann::json::const_iterator sentUsage = sent.find(REQUEST_KEY_METHOD_USAGE);
    if (sentUsage != sent.end() && sentUsage->is_object()) {
      for (nlohmann::json::const_iterator area = sentUsage->begin(); area != sentUsage->end(); ++area) {
        std::map<std::string, std::map<std::string, long long>>::iterator counted = _methodUsage.find(area.key());
        if (counted == _methodUsage.end() || !area->is_object()) {
          continue;
        }
        for (nlohmann::json::const_iterator method = area->begin(); method != area->end(); ++method) {
          if (method->is_number()) {
            subtract(counted->second, method.key(), method->get<long long>());
          }
        }
        if (counted->second.empty()) {
          _methodUsage.erase(counted);
        }
      }
    }

    nlohmann::json::const_iterator sentCodes = sent.find(REQUEST_KEY_LOG_CODES);
    if (sentCodes != sent.end() && sentCodes->is_object()) {
      for (nlohmann::json::const_iterator code = sentCodes->begin(); code != sentCodes->end(); ++code) {
        if (code->is_number()) {
          subtract(_logCodes, code.key(), code->get<long long>());
        }
      }
    }
  }

  saveState();
}

void HealthCheckModule::saveState() {
  std::shared_ptr<StorageModuleBase> storage;
  std::string state;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    storage = _storage.lock();
    if (!storage) {
      return;
    }
    state = toJson(true).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  }
  // written outside the lock: the storage logs, and a warning it logs is counted here
  storage->storeHealthCheckState(state);
}

void HealthCheckModule::clearAndSave() {
  std::shared_ptr<StorageModuleBase> storage;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    _countLogWarning = 0;
    _countLogError = 0;
    _statusCode = -1;
    _errorMessage.clear();
    _countBackoffRequest = 0;
    _consecutiveBackoffRequest = 0;
    _consecutiveBackoffRequestCounter = 0;
    _methodUsage.clear();
    _logCodes.clear();
    storage = _storage.lock();
  }
  if (storage) {
    storage->storeHealthCheckState("");
  }
}
} // namespace cly
