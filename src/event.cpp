#include "countly.hpp"

cly::Countly::Event::Event(const std::string &key, size_t count) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
}

cly::Countly::Event::Event(const std::string &key, size_t count, double sum) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
}

cly::Countly::Event::Event(const std::string& key, size_t count, double sum, double duration) : object({}), timer_running(false) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
	object["dur"] = duration;
}

void cly::Countly::Event::setTimestamp() {
	timestamp = cly::Countly::getTimestamp();
	object["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
}

void cly::Countly::Event::startTimer() {
	setTimestamp();
	timer_running = true;
}

void cly::Countly::Event::stopTimer() {
	if (timer_running) {
		auto now = Countly::getTimestamp();
		object["dur"] = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
		timer_running = false;
	}
}

std::string cly::Countly::Event::serialize() const {
	return object.dump();
}
