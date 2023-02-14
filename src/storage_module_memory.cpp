
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
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront request = " + request->_data);
  if (request_queue.size() > 0 && request != nullptr && request->_id == request_queue.front()->_id) {
    request_queue.pop_front();
  }
}

int StorageModuleMemory::RQCount() {
  int size = request_queue.size();
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQCount size = " + size);
  return size;
}

void StorageModuleMemory::RQInsertAtEnd(const char *request) {
  std::string req(request);
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQInsertAtEnd request = " + req);
  if (req != "") {
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
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQPeekFront request = " + front->_data);
  }

  return front;
}

}; // namespace cly