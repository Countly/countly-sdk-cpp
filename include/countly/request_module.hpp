#ifndef REQUEST_MODULE_HPP_
#define REQUEST_MODULE_HPP_
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "countly/constants.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/event.hpp"
#include "countly/logger_module.hpp"
#include "countly/request_builder.hpp"

#ifdef _WIN32
#undef ERROR
#endif

namespace cly {
class RequestModule {

public:
  ~RequestModule();
  RequestModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder);

  HTTPResponse sendHTTP(const std::string &path, std::string data);

  /**
   * SDK central execution call for processing requests in the request queue.
   * Only one sender is active at a time. Requests are processed in order.
   */
  void processQueue(std::shared_ptr<std::mutex> mutex);

  void addRequestToQueue(const std::map<std::string, std::string> &data);

private:
  class RequestModuleImpl;
  std::unique_ptr<RequestModuleImpl> impl;
};
} // namespace cly
#endif
