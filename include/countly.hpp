#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include <chrono>
#include <cstddef>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <set>

#ifndef COUNTLY_USE_SQLITE
#include <deque>
#endif

#define COUNTLY_SDK_VERSION "0.1.0"
#define COUNTLY_API_VERSION "19.8.0"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000

class Countly {
public:
	Countly();

	~Countly();

	static Countly& getInstance();

	// Do not implicitly generate the copy constructor, this is a singleton.
	Countly(const Countly&) = delete;

	// Do not implicitly generate the copy assignment operator, this is a singleton.
	void operator=(const Countly&) = delete;

	void alwaysUsePost(bool value);

	void setSalt(const std::string& value);

	enum LogLevel {DEBUG, INFO, WARNING, ERROR, FATAL};

	void setLogger(void (*fun)(LogLevel level, const std::string& message));

	struct HTTPResponse {
		bool success;
		std::map<std::string, std::string> data;
	};

	void setHTTPClient(HTTPResponse (*fun)(bool use_post, const std::string& url, const std::string& data));

	void setMetrics(const std::string& os, const std::string& os_version, const std::string& device, const std::string& resolution, const std::string& carrier, const std::string& app_version);

	void start(const std::string& app_key, const std::string& device_id, const std::string& host, int port = -1, bool start_thread = false);

	void startOnCloud(const std::string& app_key, const std::string& device_id);

	void stop();

	class Event;

	void addEvent(const Event& event);

	bool beginSession();

	bool updateSession();

	bool endSession();

	static std::chrono::system_clock::time_point getTimestamp();

	static std::string encodeURL(const std::string& data);

	static std::string serializeForm(const std::map<std::string, std::string> data);

	static std::string formatJSONString(const std::string& string);

#ifdef COUNTLY_USE_SQLITE
	void setDatabasePath(const std::string& path);
#endif

	class Event {
	public:
		Event(const std::string& key, size_t count);
		Event(const std::string& key, size_t count, double sum);

		template<typename T>
		void addSegmentation(const std::string& key, T value) {
			segmentation[key] = std::to_string(value);
		}

		std::string serialize() const;
	private:
		std::string json_start;
		std::map<std::string, std::string> segmentation;
	};

private:
	void log(LogLevel level, const std::string& message);

	HTTPResponse sendHTTP(std::string path, std::string data);

	std::chrono::system_clock::duration getSessionDuration();

	void updateLoop();

	void (*logger_function)(LogLevel level, const std::string& message);
	HTTPResponse (*http_client_function)(bool is_post, const std::string& url, const std::string& data);

	std::string device_id;
	std::string app_key;

	std::string host;
	int port;
	bool use_https;
	bool always_use_post;
	std::chrono::system_clock::time_point last_sent;
	bool began_session;

	std::string metrics;
	std::string salt;

	std::thread *thread;
	std::mutex mutex;
	bool stop_thread;
	bool running;

	size_t max_events;
#ifndef COUNTLY_USE_SQLITE
	std::deque<Event> event_queue;
#else
	std::string database_path;
#endif
};

template<>
void Countly::Event::addSegmentation<const char*>(const std::string& key, const char* value);

template<>
void Countly::Event::addSegmentation<const std::string&>(const std::string& key, const std::string& value);

template<>
void Countly::Event::addSegmentation<bool>(const std::string& key, bool value);

#endif
