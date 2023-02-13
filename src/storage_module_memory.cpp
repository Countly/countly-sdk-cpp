
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

void StorageModuleMemory::RQRemoveFront(std::string &request) {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQRemoveFront request = " + request);
  if (request_queue.size() > 0 && request == request_queue.front()) {
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
    request_queue.push_back(request);
  }
}

std::vector<std::string> &StorageModuleMemory::RQPeekAll() {
  std::vector<std::string> v;
  for (int i = 0; i < request_queue.size(); ++i) {
    v.push_back(request_queue.front());
  }

  return v;
}

void StorageModuleMemory::RQClearAll() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQClearAll");
  request_queue.clear();
}

const std::string &StorageModuleMemory::RQPeekFront() {
  std::string front = "";
  if (request_queue.size() > 0) {
    front = request_queue.front();
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleMemory] RQPeekFront request = " + front);
  }

  return front;
}

}; // namespace cly