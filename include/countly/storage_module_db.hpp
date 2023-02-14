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

  void init() override;
  int RQCount() override;
  void RQClearAll() override;
  virtual void RQRemoveFront() override;
  const std::string RQPeekFront() override;
  std::vector<std::string> RQPeekAll() override;
  void RQRemoveFront(std::string &request) override;
  void RQInsertAtEnd(const std::string &request) override;
};
} // namespace cly
#endif