#include "countly/request_builder.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
namespace cly {
RequestBuilder::RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) {}

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

  std::map<std::string, std::string> request = {
      {"app_key", _configuration->appKey},
      {"device_id", _configuration->deviceId},
  };

  request.insert(data.begin(), data.end());
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
