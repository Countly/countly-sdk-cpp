#include "countly.hpp"

Event::Event(const std::string& key, size_t count) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
}

Event::Event(const std::string& key, size_t count, double sum) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
}

Event::Event(const std::string& key, size_t count, double sum, double duration) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
	object["dur"] = duration;
}

void Event::setTimestamp() {
	timestamp = Countly::getTimestamp();
	object["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
}

void Event::startTimer() {
	setTimestamp();
	timer_running = true;
}

void Event::stopTimer() {
	if (timer_running) {
		auto now = Countly::getTimestamp();
		object["dur"] = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
		timer_running = false;
	}
}

std::string Event::serialize() const {
	return object.dump();
}
