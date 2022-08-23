#ifndef EVENT_MODULE_HPP_
#define EVENT_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/logger_module.hpp"
#include "countly/event.hpp"

namespace cly {
class EventModule {

public:
  ~EventModule();
  EventModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger);

  void RecordEvent(const Event &event);
  //void RecordEvent(const std::string &key, const int count = 1, const std::map<std::string, std::string> &segmentation = {}, const double sum = 0, const double duration = 0);

private:
  class EventModuleImpl;
  std::unique_ptr<EventModuleImpl> impl;
};
} // namespace cly
#endif
