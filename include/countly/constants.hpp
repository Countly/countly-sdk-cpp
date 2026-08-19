#ifndef COUNTLY_CONSTANTS_HPP_
#define COUNTLY_CONSTANTS_HPP_

#include "nlohmann/json.hpp"
#include <cassert>
#include <chrono>
#include <climits>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#define COUNTLY_SDK_NAME "cpp-native-unknown"
#define COUNTLY_SDK_VERSION "26.8.0"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000
#define COUNTLY_MAX_EVENTS_DEFAULT 200
#define COUNTLY_MAX_KEY_LENGTH_DEFAULT 128
#define COUNTLY_MAX_VALUE_SIZE_DEFAULT 256
#define COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT 100
#define COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT 100
#define COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT 30
#define COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT 200
#define COUNTLY_MAX_VALUE_SIZE_PICTURE 4096

namespace cly {
struct HTTPResponse {
  bool success;
  nlohmann::json data;
};

using HTTPClientFunction = std::function<HTTPResponse(bool, const std::string &, const std::string &)>;
using SHA256Function = std::function<std::string(const std::string &)>;
namespace utils {

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
 * Thread-safe replacements for std::localtime and std::gmtime.
 *
 * Both of those return a pointer into a single process-wide std::tm, so two
 * threads calling them concurrently race, and a caller can end up copying the
 * struct another thread has just overwritten -- including the other function's
 * result, since localtime and gmtime share that one buffer. With more than one
 * SDK instance this needs no threads of the integrator's own: every instance
 * runs its own update loop, and each one builds requests.
 */
inline std::tm localTime(std::time_t time) {
  std::tm result = std::tm();
#if defined(_WIN32) && (defined(_MSC_VER) || defined(MINGW_HAS_SECURE_API))
  localtime_s(&result, &time);
#else
  localtime_r(&time, &result);
#endif
  return result;
}

inline std::tm gmTime(std::time_t time) {
  std::tm result = std::tm();
#if defined(_WIN32) && (defined(_MSC_VER) || defined(MINGW_HAS_SECURE_API))
  gmtime_s(&result, &time);
#else
  gmtime_r(&time, &result);
#endif
  return result;
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
 * Generate an event/view ID.
 *
 * The engine is thread_local and is *advanced* across calls. An earlier version
 * bound a copy of a const engine on each call, which meant every ID in the
 * process shared one random component; on platforms with a coarse system_clock
 * (Windows, ~15ms) the timestamp did not move either, so IDs generated inside
 * one tick were identical.
 *
 * @return a string object holding the ID.
 */
inline std::string generateEventID() {
  static thread_local std::mt19937 engine(static_cast<std::mt19937::result_type>(std::chrono::system_clock::now().time_since_epoch().count() ^ static_cast<long long>(std::hash<std::thread::id>()(std::this_thread::get_id()))));
  static thread_local std::uniform_int_distribution<int> distribution(1, INT_MAX);

  const int random = distribution(engine);

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

  virtual void RecordLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress) = 0;
};

} // namespace cly

#endif
