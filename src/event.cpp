#include "countly.hpp"

Countly::Event::Event(const std::string& key, size_t count) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
}

Countly::Event::Event(const std::string& key, size_t count, double sum) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
}

Countly::Event::Event(const std::string& key, size_t count, double sum, double duration) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
	object["dur"] = duration;
}

void Countly::Event::setTimestamp() {
	timestamp = Countly::getTimestamp();
	object["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
}

void Countly::Event::startTimer() {
	setTimestamp();
	timer_running = true;
}

void Countly::Event::stopTimer() {
	if (timer_running) {
		auto now = Countly::getTimestamp();
		object["dur"] = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
		timer_running = false;
	}
}

std::string Countly::Event::serialize() const {
	return object.dump();
}
