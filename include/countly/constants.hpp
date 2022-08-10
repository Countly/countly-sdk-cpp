#ifndef COUNTLY_CONSTANTS_HPP_
#define COUNTLY_CONSTANTS_HPP_

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <stdarg.h>
#include <stdexcept>
#include <stdio.h>
#include <string>

#define COUNTLY_SDK_NAME "cpp-native-unknown"
#define COUNTLY_SDK_VERSION "0.1.0"
#define COUNTLY_API_VERSION "21.11.2"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000
#define COUNTLY_MAX_EVENTS_DEFAULT 200

namespace cly {
using SHA256Function = std::function<std::string(const std::string &)>;
namespace utils {
const std::default_random_engine generator;
const std::uniform_int_distribution<int> distribution(1, INT_MAX);
const std::string CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
/**
 * Convert map into a string.
 *
 * @param format formatting string
 * @param args arguments to be formatted
 * @return a string object holding the formatted result.
 */
template <typename... Args> static std::string format(const std::string &format, Args... args) {
  int length = std::snprintf(nullptr, 0, format.c_str(), args...);
  assert(length >= 0);

  char *buf = new char[length + 1];
  std::snprintf(buf, length + 1, format.c_str(), args...);

  std::string str(buf);
  delete[] buf;
  return str;
}

/**
 * Convert map into a string.
 *
 * @param m a map containing key-value pairs
 * @return a string object holding content of map.
 */
static std::string mapToString(std::map<std::string, std::string> &m) {
  std::string holder = "{";
  std::string result = "";

  for (auto it = m.cbegin(); it != m.cend(); it++) {
    holder += (it->first) + ":" + (it->second) + ", ";
  }

  result = holder.substr(0, holder.size() - 2) + "}";

  return result;
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
  std::string result = std::to_string(random) + "-" + std::to_string(timestamp.count());
  return result;
}
} // namespace utils

class CountlyDelegates {
public:
  virtual void RecordEvent(const std::string key, int count) = 0;

  virtual void RecordEvent(const std::string key, int count, double sum) = 0;

  virtual void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count) = 0;

  virtual void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count, double sum) = 0;

  virtual void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count, double sum, double duration) = 0;
};

} // namespace cly

#endif
