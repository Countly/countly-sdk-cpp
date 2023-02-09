
#include "countly/storage_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>


namespace cly{
StorageModule::StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageBase(config,logger) {

}

StorageModule::~StorageModule() {}

void StorageModule::init() {
     _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] initialized.");
}

void StorageModule::RQRemoveFront() {
     _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] RQRemoveFront");
    request_queue.pop_front();
}

int StorageModule::RQCount() {
    int size = request_queue.size();
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] RQCount size = " + size);
    return size;
}

void StorageModule::RQInsertAtEnd(const std::string &request) {
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] RQInsertAtEnd request = " + request);
    request_queue.push_back(request);
}

void StorageModule::RQClearAll() {
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] RQClearAll");
    request_queue.clear();
}

std::string& StorageModule::RQPeekFront() {
    std::string& front = request_queue.front();
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModule] RQPeekFront request = " + front);
    return front;
}

};