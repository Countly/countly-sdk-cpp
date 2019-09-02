#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include <cstddef>
#include <sstream>
#include <string>
#include <map>

#ifndef COUNTLY_USE_SQLITE
#include <deque>
#endif

#define COUNTLY_SDK_VERSION "0.1.0"
#define COUNTLY_API_VERSION "19.8.0"
#define COUNTLY_POST_THRESHOLD 2000

class Countly {
public:
	Countly();

	~Countly();

	static Countly& getInstance();

	// Do not implicitly generate the copy constructor, this is a singleton.
	Countly(const Countly&) = delete;

	// Do not implicitly generate the copy assignment operator, this is a singleton.
	void operator=(const Countly&) = delete;

	enum LogLevel {DEBUG, INFO, WARNING, ERROR, FATAL};

	void setLogger(void (*fun)(LogLevel level, const std::string& message));

	void setHTTPClient(bool (*fun)(bool use_post, const std::string& url, const std::string& data));

	void start(const std::string& app_key, const std::string& host, int port = -1);

	void startOnCloud(const std::string& app_key);

	bool updateSession();

	class Event {
	public:
		Event(const std::string& key, size_t count);
		Event(const std::string& key, size_t count, double sum);

		void addSegmentationRaw(const std::string& key, const std::string& json_value);

		template<typename T>
		void addSegmentation(const std::string& key, T value) {
			segmentation[key] = std::to_string(value);
		}

		std::string serialize();
	private:
		static std::string formatString(const std::string& string);

		std::ostringstream json_start;
		std::map<std::string, std::string> segmentation;
	};

	void stop();
private:
	void log(LogLevel level, const std::string& message);

	bool sendHTTP(const std::string& path, const std::string& data);

	void (*logger_function)(LogLevel level, const std::string& message);
	bool (*http_client_function)(bool is_post, const std::string& url, const std::string& data);
	size_t max_events;
	std::string device_id;
	std::string app_key;
	std::string host;
	int port;
	bool use_https;

#ifndef COUNTLY_USE_SQLITE
	std::deque<Event> event_queue;
#endif
};

template<>
void Countly::Event::addSegmentation<const char*>(const std::string& key, const char* value);

template<>
void Countly::Event::addSegmentation<const std::string&>(const std::string& key, const std::string& value);

template<>
void Countly::Event::addSegmentation<bool>(const std::string& key, bool value);

#endif
