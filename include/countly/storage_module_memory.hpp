#ifndef STORAGE_MODULE_MEMORY_HPP_
#define STORAGE_MODULE_MEMORY_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/storage_module_base.hpp"
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cly {
class StorageModuleMemory : public StorageModuleBase {
private:
  // Guards request_queue and _lastUsedId. The module owns its synchronisation so
  // callers do not have to hold the instance mutex just to read a queue size --
  // taking that mutex from a public getter deadlocks when an integrator calls the
  // getter from their log callback, which the SDK invokes while holding it.
  //
  // Never held across a _logger->log() call: the callback may call back into the
  // SDK, and lock order would then run storage -> instance while the rest of the
  // SDK runs instance -> storage.
  std::mutex _mutex;
  long long _lastUsedId = 0;
  std::deque<std::shared_ptr<DataEntry>> request_queue;

public:
  StorageModuleMemory(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModuleMemory();

  void init() override;
  long long RQCount() override;
  void RQClearAll() override;
  virtual void RQRemoveFront() override;
  const std::shared_ptr<DataEntry> RQPeekFront() override;
  std::vector<std::shared_ptr<DataEntry>> RQPeekAll() override;
  void RQRemoveFront(std::shared_ptr<DataEntry> request) override;
  void RQInsertAtEnd(const std::string &request) override;
  void storeSDKBehaviorSettings(const std::string &sdk_behavior_settings) override;
  std::string getSDKBehaviorSettings() override;
};
} // namespace cly
#endif