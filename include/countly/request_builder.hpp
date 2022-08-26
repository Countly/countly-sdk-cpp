#ifndef REQUEST_BUILDER_HPP_
#define REQUEST_BUILDER_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "nlohmann/json.hpp"
#include <memory>
#include <string>

namespace cly {

class RequestBuilder {
private:
  std::shared_ptr<CountlyConfiguration> _configuration;

public:
  RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~RequestBuilder();
  std::string encodeURL(const std::string &data);
  std::string buildRequest(const std::map<std::string, std::string> &data);
  std::string serializeData(const std::map<std::string, std::string> &data);
};
} // namespace cly
#endif
