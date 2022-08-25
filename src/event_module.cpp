#include "countly/event_module.hpp"
#include "countly/countly_configuration.hpp"

#include <chrono>
#include <deque>

namespace cly {
class EventModule::EventModuleImpl {

private:
  cly::CountlyDelegates *_cly;

public:
  std::shared_ptr<RequestModule> _requestModule;
  const CountlyConfiguration _configuration;
  std::deque<std::string> event_queue;
  std::shared_ptr<cly::LoggerModule> _logger;
  EventModuleImpl(const CountlyConfiguration &config, cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger, std::shared_ptr<RequestModule> requestModule) : _cly(cly), _logger(logger), _configuration(config), _requestModule(requestModule) {}

  ~EventModuleImpl() { _logger.reset(); }
};

EventModule::EventModule(const CountlyConfiguration &config, CountlyDelegates *cly, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule) {
  impl.reset(new EventModuleImpl(config, cly, logger, requestModule));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[EventModule] Initialized"));
}

EventModule::~EventModule() { impl.reset(); }

void EventModule::RecordEvent(const Event &event) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[EventModule] RecordEvent: " + event.serialize()));

  if (impl->_configuration.eventQueueThreshold == impl->event_queue.size()) {
    addEventsToRequestQueue();
    impl->_requestModule->processQueue();
  }
}

void EventModule::RecordEvent(const std::string &key, const int count, const std::map<std::string, std::string> &segmentation, const double sum, const double duration) {
  Event event(key, count, sum, duration);

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[EventModule] RecordEvent: " + event.serialize()));
  for (auto key_value : segmentation) {
    event.addSegmentation(key_value.first, key_value.second);
  }

  RecordEvent(event);
}

void EventModule::addEventsToRequestQueue() {
  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[EventModule] addEventsToRequestQueue: Start"));

  if (impl->event_queue.empty()) {
    impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[EventModule] addEventsToRequestQueue: Event queue is empty!"));
    return;
  }

  nlohmann::json events = nlohmann::json::array();
  for (const auto &event_json : impl->event_queue) {
    events.push_back(nlohmann::json::parse(event_json));
  }
  std::map<std::string, std::string> data = {{"events", events.dump()}};

  impl->_requestModule->addRequestToQueue(data);
}

} // namespace cly