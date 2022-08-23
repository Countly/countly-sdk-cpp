#ifndef EVENT_MODULE_HPP_
#define EVENT_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/event.hpp"
#include "countly/logger_module.hpp"

namespace cly {
class RequestModule {

public:
  ~RequestModule();
  RequestModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger);

  void processQueue();
  void addRequestToQueue(std::string &request);

private:
  class RequestModuleImpl;
  std::unique_ptr<RequestModuleImpl> impl;
};
} // namespace cly
#endif
