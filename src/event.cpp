#include "countly.hpp"

Countly::Event::Event(const std::string& key, size_t count) : object({}) {
	object["key"] = key;
	object["count"] = count;
}

Countly::Event::Event(const std::string& key, size_t count, double sum) : object({}) {
	object["key"] = key;
	object["count"] = count;
	object["sum"] = sum;
}

std::string Countly::Event::serialize() const {
	return object.dump();
}
