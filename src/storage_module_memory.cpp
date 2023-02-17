
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

void StorageModuleMemory::init() { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] initialized."); }

void StorageModuleMemory::RQRemoveFront() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront");
  if (request_queue.size() > 0) {
    request_queue.pop_front();
  }
}

void StorageModuleMemory::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (request == nullptr) {
    return;
  }

  if (request_queue.size() > 0 && request.get() == request_queue.front().get()) {
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront request = " + request->getData());
    request_queue.pop_front();
  }
}

int StorageModuleMemory::RQCount() {
  int size = request_queue.size();
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQCount size = " + size);
  return size;
}

void StorageModuleMemory::RQInsertAtEnd(const std::string &request) {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQInsertAtEnd request = " + request);
  if (request != "") {
    _lastUsedId += 1;
    std::shared_ptr<DataEntry> entry = std::make_shared<DataEntry>(_lastUsedId, request);
    entry.reset(new DataEntry(_lastUsedId, request));
    request_queue.push_back(entry);
  }
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleMemory::RQPeekAll() {
  int qSize = request_queue.size();
  std::vector<std::shared_ptr<DataEntry>> v(qSize);
  for (int i = 0; i < request_queue.size(); ++i) {
    v[i] = request_queue.at(i);
  }

  return v;
}

void StorageModuleMemory::RQClearAll() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQClearAll");
  request_queue.clear();
}

const std::shared_ptr<DataEntry> StorageModuleMemory::RQPeekFront() {
  std::shared_ptr<DataEntry> front = nullptr;
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