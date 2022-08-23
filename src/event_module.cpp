#include "countly/event_module.hpp"

#include <chrono>

namespace cly {
class EventModule::EventModuleImpl {

private:
  cly::CountlyDelegates *_cly;

public:
  std::shared_ptr<cly::LoggerModule> _logger;
  EventModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly(cly), _logger(logger) {}

  ~EventModuleImpl() { _logger.reset(); }
};

EventModule::EventModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new EventModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[ViewsModule] Initialized"));
}

EventModule::~EventModule() { impl.reset(); }

void EventModule::RecordEvent(const Event &event) {
}

void EventModule::RecordEvent(const std::string &key, const int count, const std::map<std::string, std::string> &segmentation, const double sum, const double duration) {
  Event event(key, count, sum, duration);

  for (auto key_value : segmentation) {
    event.addSegmentation(key_value.first, key_value.second);
  }

  RecordEvent(event);
}

} // namespace cly