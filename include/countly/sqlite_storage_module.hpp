#ifndef SQLITE_STORAGE_MODULE_HPP_
#define SQLITE_STORAGE_MODULE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_base.hpp"
#include <deque>
#include <memory>
#include <string>

namespace cly {
class SqliteStorageModule : StorageBase {
private:

public:
  SqliteStorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~SqliteStorageModule();

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