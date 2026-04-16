#include "countly/event.hpp"
#include <ctime>

namespace cly {
Event::Event(const std::string &key, size_t count) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
  setTimestamp();
}

Event::Event(const std::string &key, size_t count, double sum) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
  object["sum"] = sum;
  setTimestamp();
}

Event::Event(const std::string &key, size_t count, double sum, double duration) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
  object["sum"] = sum;
  object["dur"] = duration;

  setTimestamp();
}

void Event::setTimestamp() {
  timestamp = std::chrono::system_clock::now();
  object["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();

  std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
  std::tm local_tm = *std::localtime(&time);
  object["dow"] = local_tm.tm_wday;
  object["hour"] = local_tm.tm_hour;
}

void Event::startTimer() {
  setTimestamp();
  timer_running = true;
}

void Event::stopTimer() {
  if (timer_running) {
    auto now = std::chrono::system_clock::now();
    object["dur"] = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
    timer_running = false;
  }
}

std::string Event::serialize() const { return object.dump(); }

std::string Event::getKey() const {
  auto it = object.find("key");
  if (it != object.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return "";
}

bool Event::hasSegmentation() const { return object.find("segmentation") != object.end() && object["segmentation"].is_object() && !object["segmentation"].empty(); }

void Event::removeSegmentation(const std::string &key) {
  if (object.find("segmentation") != object.end()) {
    object["segmentation"].erase(key);
    if (object["segmentation"].empty()) {
      object.erase("segmentation");
    }
  }
}

void Event::clearSegmentation() {
  object.erase("segmentation");
}
} // namespace cly
