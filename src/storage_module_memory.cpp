
#include "countly/storage_module_memory.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>

namespace cly {
StorageModuleMemory::StorageModuleMemory(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleMemory::~StorageModuleMemory() {}

void StorageModuleMemory::init() { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] initialized."); }

void StorageModuleMemory::RQRemoveFront() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront");
  if (request_queue.size() > 0) {
    request_queue.pop_front();
  }
}

void StorageModuleMemory::RQRemoveFront(const DataEntry *request) {
  if (request == nullptr) {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQRemoveFront the request pointer does not point to any valid object.");
    return;
  }

  if (request_queue.size() > 0 && request->getId() == request_queue.front()->getId()) {
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront request = " + request->getData());
    request_queue.pop_front();
    delete request;
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
    DataEntry *entry = new DataEntry(1, request);
    request_queue.push_back(entry);
  }
}

std::vector<DataEntry *> StorageModuleMemory::RQPeekAll() {
  std::vector<DataEntry *> v;
  for (int i = 0; i < request_queue.size(); ++i) {
    v.push_back(request_queue.at(i));
  }

  return v;
}

void StorageModuleMemory::RQClearAll() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQClearAll");
  request_queue.clear();
}

const DataEntry *StorageModuleMemory::RQPeekFront() {
  DataEntry *front = nullptr;
  if (request_queue.size() > 0) {
    front = request_queue.front();
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQPeekFront request = " + front->getData());
  } else {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQPeekFront Request queue is empty.");
  }

  return front;
}
}; // namespace cly