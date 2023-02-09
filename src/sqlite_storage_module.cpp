
#include "countly/sqlite_storage_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>


namespace cly{
SqliteStorageModule::SqliteStorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) :  StorageBase(config,logger) {

}

SqliteStorageModule::~SqliteStorageModule() {}

void SqliteStorageModule::init() {
    _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] initialized.");
    // TODO
    // 1. Database creation
    // 2. Schema creation
    // 3. Migration(if needed)
}

void SqliteStorageModule::RQRemoveFront() {
    _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] RQRemoveFront");

}

int SqliteStorageModule::RQCount() {
    int size = 0;
    _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] RQCount size = " + size);

    return 0;
}

void SqliteStorageModule::RQInsertAtEnd(const std::string &request) {
     _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] RQInsertAtEnd request = " + request);

}

void SqliteStorageModule::RQClearAll() {
    _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] RQClearAll");

}

std::string& SqliteStorageModule::RQPeekFront() {
    std::string front = "";
    _logger->log(LogLevel::DEBUG, "[Countly][SqliteStorageModule] RQPeekFront request = " + front);
    return front;
}

};