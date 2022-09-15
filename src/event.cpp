#include "countly/event.hpp"

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
} // namespace cly
