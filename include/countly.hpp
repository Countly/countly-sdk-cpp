#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include <map>

#ifndef COUNTLY_USE_SQLITE
#include <deque>
#endif

#include "nlohmann/json.hpp"
using json = nlohmann::json;

#define COUNTLY_SDK_NAME "language-origin-platform"
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
		json data;
	};

	void setHTTPClient(HTTPResponse (*fun)(bool use_post, const std::string& url, const std::string& data));

	void setMetrics(const std::string& os, const std::string& os_version, const std::string& device, const std::string& resolution, const std::string& carrier, const std::string& app_version);

	void setDeviceID(const std::string& value, bool same_user = false);

	void start(const std::string& app_key, const std::string& host, int port = -1, bool start_thread = false);

	void startOnCloud(const std::string& app_key);

	void stop();

	class Event;

	void addEvent(const Event& event);

	void flushEvents(std::chrono::seconds timeout = std::chrono::seconds(30));

	bool beginSession();

	bool updateSession();

	bool endSession();

	static std::chrono::system_clock::time_point getTimestamp();

	static std::string encodeURL(const std::string& data);

	static std::string serializeForm(const std::map<std::string, std::string> data);

#ifdef COUNTLY_USE_SQLITE
	void setDatabasePath(const std::string& path);
#endif

	class Event {
	public:
		Event(const std::string& key, size_t count = 1);
		Event(const std::string& key, size_t count, double sum);

		void setTimestamp();

		void startTimer();
		void stopTimer();

		template<typename T>
		void addSegmentation(const std::string& key, T value) {
			if (object.find("segmentation") == object.end()) {
				object["segmentation"] = json::object();
			}

			object["segmentation"][key] = value;
		}

		std::string serialize() const;
	private:
		json object;
		bool timer_running;
		std::chrono::system_clock::time_point timestamp;
	};

private:
	void log(LogLevel level, const std::string& message);

	HTTPResponse sendHTTP(std::string path, std::string data);

	std::chrono::system_clock::duration getSessionDuration();

	void updateLoop();

	void (*logger_function)(LogLevel level, const std::string& message);
	HTTPResponse (*http_client_function)(bool is_post, const std::string& url, const std::string& data);

	std::string old_device_id;
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
	std::deque<std::string> event_queue;
#else
	std::string database_path;
#endif
};

#endif
