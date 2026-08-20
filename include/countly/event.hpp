#ifndef EVENT_HPP_
#define EVENT_HPP_
#include "nlohmann/json.hpp"
#include <chrono>
#include <string>

namespace cly {

class Event {
public:
  Event(const std::string &key, size_t count = 1);
  Event(const std::string &key, size_t count, double sum);
  Event(const std::string &key, size_t count, double sum, double duration);

  void setTimestamp();

  void startTimer();
  void stopTimer();

  template <typename T> void addSegmentation(const std::string &key, T value) {
    if (object.find("segmentation") == object.end()) {
      object["segmentation"] = nlohmann::json::object();
    }

    object["segmentation"][key] = value;
  }

  std::string serialize() const;

  std::string getKey() const;

  bool hasSegmentation() const;

  void removeSegmentation(const std::string &key);

  void clearSegmentation();

  // Enforce SDK internal limits on this event's key and developer segmentation.
  void applyLimits(unsigned int maxKeyLength, unsigned int maxValueSize, unsigned int maxSegmentationValues);

private:
  nlohmann::json object;
  bool timer_running;
  std::chrono::system_clock::time_point timestamp;
  // Durations are measured on the monotonic clock so a system clock change
  // while a timed event runs cannot produce a wrong or negative duration.
  std::chrono::steady_clock::time_point timer_start;
};
} // namespace cly
#endif
