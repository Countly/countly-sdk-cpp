#ifndef COUNTLY_CONSTANTS_HPP_
#define COUNTLY_CONSTANTS_HPP_

#include <map>
#include <string>
#include <functional>


#define COUNTLY_SDK_NAME "cpp-native-unknown"
#define COUNTLY_SDK_VERSION "0.1.0"
#define COUNTLY_API_VERSION "21.11.2"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000
#define COUNTLY_MAX_EVENTS_DEFAULT 200

namespace countly_sdk {
	using SHA256Function = std::function<std::string(const std::string&)>;

	class CountlyDelegates {
	public:
	};
}

#endif
