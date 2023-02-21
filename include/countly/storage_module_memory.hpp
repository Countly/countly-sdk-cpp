#ifndef STORAGE_MODULE_MEMORY_HPP_
#define STORAGE_MODULE_MEMORY_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_module_base.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace cly {
class StorageModuleMemory : public StorageModuleBase {
private:
  long long _lastUsedId = 0;
  std::deque<std::shared_ptr<DataEntry>> request_queue;

public:
  StorageModuleMemory(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModuleMemory();

  void init() override;
  long long RQCount() override;
  void RQClearAll() override;
  virtual void RQRemoveFront() override;
  const std::shared_ptr<DataEntry>RQPeekFront() override;
  std::vector<std::shared_ptr<DataEntry>> RQPeekAll() override;
  void RQRemoveFront(std::shared_ptr<DataEntry>request) override;
  void RQInsertAtEnd(const std::string &request) override;
};
} // namespace cly
#endif