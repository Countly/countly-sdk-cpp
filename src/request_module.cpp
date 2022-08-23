#include "countly/request_module.hpp"

#include <chrono>
#include <deque>

namespace cly {
class RequestModule::RequestModuleImpl {

private:
  cly::CountlyDelegates *_cly;

  std::deque<std::string> request_queue;

public:
  std::shared_ptr<cly::LoggerModule> _logger;
  RequestModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly(cly), _logger(logger) {}

  ~RequestModuleImpl() { _logger.reset(); }
};

RequestModule::RequestModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new RequestModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[ViewsModule] Initialized"));
}

RequestModule::~RequestModule() { impl.reset(); }

void RequestModule::addRequestToQueue(std::string &request) {
}


} // namespace cly