#ifndef STORAGE_MODULE_HPP_
#define STORAGE_MODULE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <memory>
#include <string>

namespace cly {

class StorageModule {
private:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;

public:
  StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModule();

};
} // namespace cly
#endif