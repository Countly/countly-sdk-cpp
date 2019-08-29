#include "countly.hpp"

Countly::Countly() {}

Countly& Countly::getInstance() {
	static Countly instance;
	return instance;
}
