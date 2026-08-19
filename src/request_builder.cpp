#include "countly/request_builder.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace cly {
RequestBuilder::RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : _configuration(config), _logger(logger) {}

RequestBuilder::~RequestBuilder() {}

std::string RequestBuilder::encodeURL(const std::string &data) {
  std::ostringstream encoded;

  for (unsigned char character : data) {
    if (std::isalnum(character) || character == '.' || character == '_' || character == '~') {
      encoded << character;
    } else {
      encoded << '%' << std::setw(2) << std::hex << std::setfill('0') << std::uppercase << (unsigned int)((unsigned char)character);
    }
  }

  return encoded.str();
}

std::string RequestBuilder::buildRequest(const std::map<std::string, std::string> &data) {
  const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::time_t time = std::chrono::system_clock::to_time_t(now);
  // Not std::localtime/std::gmtime: they share one process-wide std::tm, so a
  // concurrent request build (each instance has its own update loop) can leave
  // local_tm holding GMT fields and silently corrupt tz/dow/hour.
  std::tm local_tm = cly::utils::localTime(time);
  std::tm gm_tm = cly::utils::gmTime(time);

  int tz_offset_minutes = (local_tm.tm_hour - gm_tm.tm_hour) * 60 + (local_tm.tm_min - gm_tm.tm_min);
  // Adjust for day boundary crossings
  int day_diff = local_tm.tm_mday - gm_tm.tm_mday;
  if (day_diff > 1) {
    day_diff = -1; // end of month wrap
  } else if (day_diff < -1) {
    day_diff = 1; // end of month wrap
  }
  tz_offset_minutes += day_diff * 24 * 60;

  std::map<std::string, std::string> request = {{"app_key", _configuration->appKey},
                                                 {"device_id", _configuration->deviceId},
                                                 {"timestamp", std::to_string(timestamp.count())},
                                                 {"dow", std::to_string(local_tm.tm_wday)},
                                                 {"hour", std::to_string(local_tm.tm_hour)},
                                                 {"tz", std::to_string(tz_offset_minutes)}};

  request.insert(data.begin(), data.end());
  return serializeData(request);
}

std::string RequestBuilder::serializeData(const std::map<std::string, std::string> &data) {
  std::ostringstream serialized;

  for (const auto &key_value : data) {
    serialized << key_value.first << "=" << encodeURL(key_value.second) << '&';
  }

  std::string serialized_string = serialized.str();
  if (serialized_string.size() > 0 ) {
    serialized_string.resize(serialized_string.size() - 1);
  }
  
  return serialized_string;
}
} // namespace cly
