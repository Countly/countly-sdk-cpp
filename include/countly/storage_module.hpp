#ifndef STORAGE_MODULE_HPP_
#define STORAGE_MODULE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_base.hpp"
#include <deque>
#include <memory>
#include <string>

namespace cly {
class StorageModule : public StorageBase {
private:
  std::deque<std::string> request_queue;

public:
  StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModule();

 std::string *RQPeekAll();

    virtual void init() override;
    virtual void RQClearAll() override;
    virtual void RQInsertAtEnd(const std::string &request) override;
    virtual std::string& RQPeekFront() override;
    virtual void RQRemoveFront() override;
    virtual int RQCount() override;

};
} // namespace cly
#endif