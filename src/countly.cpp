#include "countly.hpp"

Countly::Countly() {}

Countly& Countly::getInstance() {
	static Countly instance;
	return instance;
}

std::string Countly::detectOSName() {
#if defined(TARGET_OS_IPHONE)
	return "iOS";
#elif defined(TARGET_OS_MAC)
	return "macOS";
#elif defined(_WIN32) || defined(_WIN64) || defined(WIN32)
	return "Windows";
#elif defined(__linux__)
	return "Linux";
#elif defined(__OpenBSD__)
	return "Open BSD";
#elif defined(__unix__)
	return "Unix";
#else
	return "Unknown";
#endif
}
