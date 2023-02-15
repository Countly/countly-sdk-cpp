
#include "countly/storage_module_db.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>

namespace cly {
StorageModuleDB::StorageModuleDB(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleDB::~StorageModuleDB() {}

void StorageModuleDB::init() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] initialized.");
  // TODO
  // 1. Database creation
  // 2. Schema creation
  // 3. Migration(if needed)
}

void StorageModuleDB::RQRemoveFront() { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront"); }

void StorageModuleDB::RQRemoveFront(const DataEntry *request) { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront request = " + request->getData()); }

int StorageModuleDB::RQCount() {
  int size = 0;
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount size = " + size);

  return 0;
}

std::vector<DataEntry *> StorageModuleDB::RQPeekAll() {
  std::vector<DataEntry *> v;

  return v;
}

void StorageModuleDB::RQInsertAtEnd(const std::string &request) {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQInsertAtEnd request = " + request);
}

void StorageModuleDB::RQClearAll() { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQClearAll"); }

const DataEntry *StorageModuleDB::RQPeekFront() {
  const DataEntry *front = nullptr;
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFrontssssssssss");
  return front;
}

}; // namespace cly