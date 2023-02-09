
#include "countly/sqlite_storage_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>


namespace cly{
SqliteStorageModule::SqliteStorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) :  StorageBase(config,logger) {

}

SqliteStorageModule::~SqliteStorageModule() {}

void SqliteStorageModule::init() {
}

void SqliteStorageModule::RQRemoveFront() {
}

int SqliteStorageModule::RQCount() {
    return 0;
}

void SqliteStorageModule::RQInsertAtEnd(const std::string &request) {
}

void SqliteStorageModule::RQClearAll() {
}

std::string& SqliteStorageModule::RQPeekFront() {
    std::string a = "";
    return a;
}

};