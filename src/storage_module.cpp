
#include "countly/storage_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>


namespace cly{
StorageModule::StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : _configuration(config), _logger(logger) {}

StorageModule::~StorageModule() {}


}