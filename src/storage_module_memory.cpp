
#include "countly/storage_module_memory.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>
#include <mutex>

namespace cly {
StorageModuleMemory::StorageModuleMemory(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleMemory::~StorageModuleMemory() {
  _configuration.reset();
  _logger.reset();
}

void StorageModuleMemory::init() {
  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] init, Initialized.");
  _is_initialized = true;
}

void StorageModuleMemory::RQRemoveFront() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQRemoveFront, Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQRemoveFront, Start");
  {
    std::lock_guard<std::mutex> lk(_mutex);
    if (request_queue.size() > 0) {
      request_queue.pop_front();
    }
  }
}

void StorageModuleMemory::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQRemoveFront, Module is not initialized");
    return;
  }

  if (request == nullptr) {
    _logger->log(LogLevel::WARNING, "[Countly] [StorageModuleMemory] RQRemoveFront, request is null");
    return;
  }

  // The removed entry is captured under the lock and logged after it, so the log
  // callback never runs with the queue locked.
  std::shared_ptr<DataEntry> removed;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    if (request_queue.size() > 0 && request->getId() == request_queue.front()->getId()) {
      removed = request_queue.front();
      request_queue.pop_front();
    }
  }

  if (removed != nullptr) {
    _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQRemoveFront, Removing request = [" + removed->getData() + "]");
  }
}

long long StorageModuleMemory::RQCount() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQCount, Module is not initialized");
    return -1;
  }

  long long size = 0;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    size = request_queue.size();
  }
  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQCount, size = [" + std::to_string(size) + "]");
  return size;
}

void StorageModuleMemory::RQInsertAtEnd(const std::string &request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQInsertAtEnd, Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQInsertAtEnd, request = [" + request + "]");
  if (request != "") {
    std::lock_guard<std::mutex> lk(_mutex);
    if (request_queue.empty()) {
      // Since the DB (Sqlite) storage module reset the primary key when all rows get deleted. To sync with the DB storage module, the memory storage module also reset '_lastUsedId' when the request queue is empty.
      _lastUsedId = 1;
    } else {
      _lastUsedId += 1;
    }

    std::shared_ptr<DataEntry> entry(new DataEntry(_lastUsedId, request));
    request_queue.push_back(entry);
  } else {
    _logger->log(LogLevel::WARNING, "[Countly] [StorageModuleMemory] RQInsertAtEnd, request is empty");
  }
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleMemory::RQPeekAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQPeekAll, Module is not initialized");
    return {};
  }

  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQPeekAll, Start");
  std::lock_guard<std::mutex> lk(_mutex);
  std::vector<std::shared_ptr<DataEntry>> v(request_queue.begin(), request_queue.end());
  return v;
}

void StorageModuleMemory::RQClearAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQClearAll, Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQClearAll, Start");
  std::lock_guard<std::mutex> lk(_mutex);
  request_queue.clear();
}

void StorageModuleMemory::storeSDKBehaviorSettings(const std::string &sdk_behavior_settings) {
  // For in-memory storage, it is already stored in memory inside the module.
}

std::string StorageModuleMemory::getSDKBehaviorSettings() {
  // For in-memory storage, it is already stored in memory inside the module.
  return "";
}

const std::shared_ptr<DataEntry> StorageModuleMemory::RQPeekFront() {
  std::shared_ptr<DataEntry> front = nullptr;
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly] [StorageModuleMemory] RQPeekFront, Module is not initialized");
    front.reset(new DataEntry(-1, ""));
    return front;
  }

  {
    std::lock_guard<std::mutex> lk(_mutex);
    if (request_queue.size() > 0) {
      front = request_queue.front();
    }
  }

  // Logged outside the lock, see the note on _mutex.
  if (front != nullptr) {
    _logger->log(LogLevel::DEBUG, "[Countly] [StorageModuleMemory] RQPeekFront, request = [" + front->getData() + "]");
  } else {
    front.reset(new DataEntry(-1, ""));
    _logger->log(LogLevel::WARNING, "[Countly] [StorageModuleMemory] RQPeekFront, Request queue is empty.");
  }

  return front;
}
}; // namespace cly
