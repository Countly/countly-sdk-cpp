#ifndef REQUEST_BUILDER_HPP_
#define REQUEST_BUILDER_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include <memory>
#include <string>

namespace cly {

class RequestBuilder {
private:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;

public:
  RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~RequestBuilder();
  static std::string encodeURL(const std::string &data);
  static std::string serializeData(const std::map<std::string, std::string> &data);
  std::string buildRequest(const std::map<std::string, std::string> &data);

  /**
   * Builds a request that is sent at once instead of being queued, such as a remote
   * config or SDK Behavior Settings fetch: the given parameters plus the SDK name and
   * version that every request carries.
   */
  std::string buildFetchRequest(const std::map<std::string, std::string> &data);
};
} // namespace cly
#endif
