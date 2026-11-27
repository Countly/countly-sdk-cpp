#include "countly/request_builder.hpp"
#include <cctype>
#include <chrono>
#include <ctime>
#include <numeric>
namespace cly {
namespace {
/**
 * Whether URL encoding keeps the character as it is.
 */
bool keptInURL(unsigned char character) { return std::isalnum(character) || character == '.' || character == '_' || character == '~'; }

/**
 * The length of the URL-encoded form of the text.
 */
size_t encodedLength(const std::string &data) {
  return std::accumulate(data.begin(), data.end(), size_t{0}, [](size_t length, char character) { return length + (keptInURL(static_cast<unsigned char>(character)) ? 1 : 3); });
}

/**
 * Appends the URL-encoded form of the text to 'target': every character that URL encoding does not keep becomes "%"
 * and two uppercase hexadecimal digits.
 */
void appendEncoded(std::string &target, const std::string &data) {
  static const char hexDigits[] = "0123456789ABCDEF";
  for (unsigned char character : data) {
    if (keptInURL(character)) {
      target.push_back(static_cast<char>(character));
    } else {
      target.push_back('%');
      target.push_back(hexDigits[character >> 4]);
      target.push_back(hexDigits[character & 0x0F]);
    }
  }
}
} // namespace

RequestBuilder::RequestBuilder(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : _configuration(config), _logger(logger) {}

RequestBuilder::~RequestBuilder() {}

std::string RequestBuilder::encodeURL(const std::string &data) {
  std::string encoded;
  encoded.reserve(encodedLength(data));
  appendEncoded(encoded, data);
  return encoded;
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
                                                 {"t", std::to_string(static_cast<int>(_configuration->deviceIdType))},
                                                 {"timestamp", std::to_string(timestamp.count())},
                                                 {"dow", std::to_string(local_tm.tm_wday)},
                                                 {"hour", std::to_string(local_tm.tm_hour)},
                                                 {"tz", std::to_string(tz_offset_minutes)}};

  nlohmann::json::const_iterator appVersion = _configuration->metrics.find("_app_version");
  if (appVersion != _configuration->metrics.end() && appVersion->is_string() && !appVersion->get<std::string>().empty()) {
    request["av"] = appVersion->get<std::string>();
  }

  request.insert(data.begin(), data.end());
  // the SDK keys win over every other key, as in buildFetchRequest, without a second copy of the data
  request["sdk_name"] = COUNTLY_SDK_NAME;
  request["sdk_version"] = COUNTLY_SDK_VERSION;
  return serializeData(request);
}

std::string RequestBuilder::buildFetchRequest(const std::map<std::string, std::string> &data) {
  std::map<std::string, std::string> request = {{"sdk_name", COUNTLY_SDK_NAME}, {"sdk_version", COUNTLY_SDK_VERSION}};

  request.insert(data.begin(), data.end());
  return serializeData(request);
}

std::string RequestBuilder::serializeData(const std::map<std::string, std::string> &data) {
  const size_t length = std::accumulate(data.begin(), data.end(), size_t{0}, [](size_t total, const std::pair<const std::string, std::string> &key_value) { return total + key_value.first.size() + encodedLength(key_value.second) + 2; });

  std::string serialized;
  serialized.reserve(length);
  for (const auto &key_value : data) {
    if (!serialized.empty()) {
      serialized.push_back('&');
    }
    serialized.append(key_value.first);
    serialized.push_back('=');
    appendEncoded(serialized, key_value.second);
  }
  return serialized;
}
} // namespace cly
