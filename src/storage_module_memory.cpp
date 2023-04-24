
#include "countly/storage_module_memory.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>

namespace cly {
StorageModuleMemory::StorageModuleMemory(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleMemory::~StorageModuleMemory() {
  _configuration.reset();
  _logger.reset();
}

void StorageModuleMemory::init() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] initialized.");
  _is_initialized = true;
}

void StorageModuleMemory::RQRemoveFront() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQRemoveFront: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront");
  if (request_queue.size() > 0) {
    request_queue.pop_front();
  }
}

void StorageModuleMemory::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQRemoveFront(request): Module is not initialized");
    return;
  }

  if (request == nullptr) {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQRemoveFront request = null");
    return;
  }

  if (request_queue.size() > 0 && request->getId() == request_queue.front()->getId()) {
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront request = " + request->getData());
    request_queue.pop_front();
  }
}

long long StorageModuleMemory::RQCount() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQRemoveFront(request): Module is not initialized");
    return -1;
  }

  long long size = request_queue.size();
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQCount size = [" + std::to_string(size) + "]");
  return size;
}

void StorageModuleMemory::RQInsertAtEnd(const std::string &request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQInsertAtEnd: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQInsertAtEnd request = " + request);
  if (request != "") {
    if (request_queue.empty()) {
      // Since the DB (Sqlite) storage module reset the primary key when all rows get deleted. To sync with the DB storage module, the memory storage module also reset '_lastUsedId' when the request queue is empty.
      _lastUsedId = 1;
    } else {
      _lastUsedId += 1;
    }

    std::shared_ptr<DataEntry> entry = std::make_shared<DataEntry>(_lastUsedId, request);
    entry.reset(new DataEntry(_lastUsedId, request));
    request_queue.push_back(entry);
  } else {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQInsertAtEnd request is empty");
  }
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleMemory::RQPeekAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQPeekAll: Module is not initialized");
    return {};
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQPeekAll");
  int qSize = request_queue.size();
  std::vector<std::shared_ptr<DataEntry>> v(qSize);
  for (int i = 0; i < request_queue.size(); ++i) {
    v[i] = request_queue.at(i);
  }

  return v;
}

void StorageModuleMemory::RQClearAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQClearAll: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQClearAll");
  request_queue.clear();
}

const std::shared_ptr<DataEntry> StorageModuleMemory::RQPeekFront() {
  std::shared_ptr<DataEntry> front = nullptr;
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleMemory] RQPeekFront: Module is not initialized");
    front.reset(new DataEntry(-1, ""));
    return front;
  }

  if (request_queue.size() > 0) {
    front = request_queue.front();
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQPeekFront: request = " + front->getData());
  } else {
    front.reset(new DataEntry(-1, ""));
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQPeekFront: Request queue is empty.");
  }

  return front;
}
}; // namespace cly