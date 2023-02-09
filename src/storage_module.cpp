
#include "countly/storage_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>


namespace cly{
StorageModule::StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageBase(config,logger) {

}

StorageModule::~StorageModule() {}

void StorageModule::init() {
}

void StorageModule::RQRemoveFront() {
    request_queue.pop_front();
}

int StorageModule::RQCount() {
    return request_queue.size();
}

void StorageModule::RQInsertAtEnd(const std::string &request) {
    request_queue.push_back(request);
}

void StorageModule::RQClearAll() {
    request_queue.clear();
}

std::string& StorageModule::RQPeekFront() {
    request_queue.front();
}

};