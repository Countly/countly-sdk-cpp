#ifndef COUNTLY_CONSTANTS_HPP_
#define COUNTLY_CONSTANTS_HPP_

#include "nlohmann/json.hpp"
#include <cassert>
#include <chrono>
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#define COUNTLY_SDK_NAME "cpp-native-unknown"
#define COUNTLY_SDK_VERSION "23.2.1"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000
#define COUNTLY_MAX_EVENTS_DEFAULT 200

namespace cly {
struct HTTPResponse {
  bool success;
  nlohmann::json data;
};

using HTTPClientFunction = std::function<HTTPResponse(bool, const std::string &, const std::string &)>;
using SHA256Function = std::function<std::string(const std::string &)>;
namespace utils {
const std::default_random_engine generator(std::chrono::system_clock::now().time_since_epoch().count());
const std::uniform_int_distribution<int> distribution(1, INT_MAX);

/**
 * Formats the given arguments into a string buffer.
 *
 * @param format formatting string
 * @param args arguments to be formatted
 * @return a string object holding the formatted result.
 */
template <typename... Args> static std::string format_string(const std::string &format, Args... args) {
  int length = std::snprintf(nullptr, 0, format.c_str(), args...);
  assert(length >= 0);

  std::unique_ptr<char[]> buf(new char[length + 1]);
  std::snprintf(buf.get(), length + 1, format.c_str(), args...);
  std::string str(buf.get());

  return str;
}

/**
 * Gives a string representation of the size of a map.
 *
 * @param m a map containing key-value pairs
 * @return a string object holding size of map.
 * TODO: In the future, this function will be improved.
 */
static std::string mapToString(const std::map<std::string, std::string> &m) {
  int lenght = m.size();

  return std::to_string(lenght);
}
/**
 * Generate a random UUID.
 *
 * @return a string object holding a UUID.
 */
static std::string generateEventID() {
  auto dice = std::bind(distribution, generator);
  int random = dice();

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  const auto timestamp = now.time_since_epoch();

  std::stringstream r;
  r << std::to_string(random);
  r << "_";
  r << std::to_string(timestamp.count());

  return r.str();
}

} // namespace utils

class CountlyDelegates {
public:
  virtual void RecordEvent(const std::string &key, int count) = 0;

  virtual void RecordEvent(const std::string &key, int count, double sum) = 0;

  virtual void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count) = 0;

  virtual void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum) = 0;

  virtual void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum, double duration) = 0;
};

} // namespace cly

#endif
