#ifndef STORAGE_BASE_HPP_
#define STORAGE_BASE_HPP_

namespace cly {

  class StorageBase {
    protected:
    std::shared_ptr<CountlyConfiguration> _configuration;
    std::shared_ptr<LoggerModule> _logger;

    public:
    StorageBase(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) {
      this->_configuration = config;
      this->_logger = logger;
    }

    virtual void init() = 0;
    virtual void RQClearAll() = 0;
    virtual void RQInsertAtEnd(const std::string &request) = 0;
    virtual std::string& RQPeekFront() = 0;
    virtual void RQRemoveFront() = 0;
    virtual int RQCount() = 0;
  };
}

#endif