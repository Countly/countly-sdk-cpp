#ifndef STORAGE_MODULE_DB_HPP_
#define STORAGE_MODULE_DB_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_module_base.hpp"
#include <deque>
#include <memory>
#include <string>

namespace cly {
class StorageModuleDB : public StorageModuleBase {
private:
public:
  StorageModuleDB(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModuleDB();

  std::string *RQPeekAll();

  void init() override;
  void RQClearAll() override;
  void RQInsertAtEnd(const std::string &request) override;
  const std::string &RQPeekFront() override;
  void RQRemoveFront() override;
  int RQCount() override;
};
} // namespace cly
#endif