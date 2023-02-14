#ifndef STORAGE_MODULE_BASE_HPP_
#define STORAGE_MODULE_BASE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <string>
#include <vector>

namespace cly {

class DataEntry {
public:
  long long _id;
  std::string _data;
  DataEntry(const long long id, const char * data) {
    this->_id = id;
    this->_data = data;
  }

  ~DataEntry() { 
	 
  }
};

class StorageModuleBase {
protected:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;

public:
  StorageModuleBase(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) {
    this->_configuration = config;
    this->_logger = logger;
  }
  virtual ~StorageModuleBase() {
    _logger.reset();
    _configuration.reset();
  }

  virtual void init() = 0;
  virtual int RQCount() = 0;
  virtual void RQClearAll() = 0;
  virtual void RQRemoveFront() = 0;
  const virtual DataEntry *RQPeekFront() = 0;
  virtual std::vector<DataEntry *> RQPeekAll() = 0;
  virtual void RQRemoveFront(const DataEntry *request) = 0;
  virtual void RQInsertAtEnd(const char request[]) = 0;
};

} // namespace cly

#endif