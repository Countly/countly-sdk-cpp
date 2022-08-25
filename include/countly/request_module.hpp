#ifndef REQUEST_MODULE_HPP_
#define REQUEST_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/event.hpp"
#include "countly/logger_module.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/request_builder.hpp"

#ifdef _WIN32
#undef ERROR
#endif

namespace cly {
class RequestModule {

public:
  ~RequestModule();
  RequestModule(const CountlyConfiguration &config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder);

  void processQueue();
  void addRequestToQueue(const std::map<std::string, std::string> &data);

private:
  class RequestModuleImpl;
  std::unique_ptr<RequestModuleImpl> impl;
};
} // namespace cly
#endif
