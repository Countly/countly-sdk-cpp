#ifndef COUNTLY_CONSTANTS_HPP_
#define COUNTLY_CONSTANTS_HPP_

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <stdarg.h>
#include <stdexcept>
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
static const std::string CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
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

  //*Adapted from https://gist.github.com/ne-sachirou/882192
  std::string uuid = std::string(36, ' ');
  int rnd = 0;
  int r = 0;

  uuid[8] = '-';
  uuid[13] = '-';
  uuid[18] = '-';
  uuid[23] = '-';

  uuid[14] = '4';

  for (int i = 0; i < 36; i++) {
    if (i != 8 && i != 13 && i != 18 && i != 14 && i != 23) {
      if (rnd <= 0x02) {
        rnd = 0x2000000 + (std::rand() * 0x1000000) | 0;
      }
      rnd >>= 4;
      uuid[i] = CHARS[(i == 19) ? ((rnd & 0xf) & 0x3) | 0x8 : rnd & 0xf];
    }
  }

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  const auto timestamp = now.time_since_epoch();
  return uuid + "-" + std::to_string(timestamp.count());
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
