#ifndef EVENT_MODULE_HPP_
#define EVENT_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/event.hpp"
#include "countly/logger_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/request_module.hpp"

namespace cly {
class EventModule {

public:
  ~EventModule();
  EventModule(const CountlyConfiguration &config, CountlyDelegates *cly, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule);

  /// <summary>
  /// Add all recorded events to request queue
  /// </summary>
  void addEventsToRequestQueue();

  /// <summary>
  /// Add event to the event queue.
  /// </summary>
  /// <param name="event">an event</param>
  /// <returns></returns>
  void RecordEvent(const Event &event);

  /// <summary>
  /// Add event to the event queue with segmentation.
  /// </summary>
  /// <param name="key">event key</param>
  /// <param name="segmentation">custom segmentation you want to set, leave null if you don't want to add anything</param>
  /// <param name="count">how many of these events have occurred, default value is "1"</param>
  /// <param name="sum">set sum if needed, default value is "0"</param>
  /// <param name="duration">set sum if needed, default value is "0"</param>
  /// <returns></returns>
  /// </summary>
  void RecordEvent(const std::string &key, const int count = 1, const std::map<std::string, std::string> &segmentation = {}, const double sum = 0, const double duration = 0);

private:
  class EventModuleImpl;
  std::unique_ptr<EventModuleImpl> impl;
};
} // namespace cly
#endif
