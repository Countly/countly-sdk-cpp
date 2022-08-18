#include "countly/event.hpp"

cly::Event::Event(const std::string &key, size_t count) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
}

cly::Event::Event(const std::string &key, size_t count, double sum) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
  object["sum"] = sum;
}

cly::Event::Event(const std::string &key, size_t count, double sum, double duration) : object({}), timer_running(false) {
  object["key"] = key;
  object["count"] = count;
  object["sum"] = sum;
  object["dur"] = duration;
}

void cly::Event::setTimestamp() {
  timestamp = std::chrono::system_clock::now();
  object["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
}

void cly::Event::startTimer() {
  setTimestamp();
  timer_running = true;
}

void cly::Event::stopTimer() {
  if (timer_running) {
    auto now = std::chrono::system_clock::now();
    object["dur"] = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
    timer_running = false;
  }
}

std::string cly::Event::serialize() const { return object.dump(); }
