
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

int StorageModuleDB::RQCount() {
  int size = 0;
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount size = " + size);

  return 0;
}

void StorageModuleDB::RQInsertAtEnd(const std::string &request) { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQInsertAtEnd request = " + request); }

void StorageModuleDB::RQClearAll() { _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQClearAll"); }

const std::string &StorageModuleDB::RQPeekFront() {
  const std::string front = "";
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFront request = " + front);
  return front;
}

}; // namespace cly