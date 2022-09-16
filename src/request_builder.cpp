#include "countly/request_builder.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <chrono>
namespace cly {
RequestBuilder::RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : _configuration(config), _logger(logger) {}

RequestBuilder::~RequestBuilder() {}

std::string RequestBuilder::encodeURL(const std::string &data) {
  std::ostringstream encoded;

  for (auto character : data) {
    if (std::isalnum(character) || character == '.' || character == '_' || character == '~') {
      encoded << character;
    } else {
      encoded << '%' << std::setw(2) << std::hex << std::uppercase << (int)((unsigned char)character);
    }
  }

  return encoded.str();
}

std::string RequestBuilder::buildRequest(const std::map<std::string, std::string> &data) {
  const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::map<std::string, std::string> request = {
      {"app_key", _configuration->appKey},
    //  {"device_id", _configuration->deviceId}, 
      {"timestamp", std::to_string(timestamp.count())}
  };

  request.insert(data.begin(), data.end());
  return serializeData(request);
}

  std::string RequestBuilder::serializeData(const std::map<std::string, std::string> &data) {
    std::ostringstream serialized;

    for (const auto &key_value : data) {
      serialized << key_value.first << "=" << encodeURL(key_value.second) << '&';
    }

    std::string serialized_string = serialized.str();
    serialized_string.resize(serialized_string.size() - 1);

    return serialized_string;
  }
} // namespace cly
