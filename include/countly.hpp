#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include <iterator>
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

#ifdef _WIN32
#undef ERROR
#endif

#define COUNTLY_SDK_NAME "cpp-native-unknown"
#define COUNTLY_SDK_VERSION "0.1.0"
#define COUNTLY_API_VERSION "21.11.0"
#define COUNTLY_POST_THRESHOLD 2000
#define COUNTLY_KEEPALIVE_INTERVAL 3000
#define COUNTLY_MAX_EVENTS_DEFAULT 200

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

	void setUserDetails(const std::map<std::string, std::string>& value);

	void setCustomUserDetails(const std::map<std::string, std::string>& value);

	void setCountry(const std::string& country_code);

	void setCity(const std::string& city_name);

	void setLocation(double lattitude, double longitude);

	void setDeviceID(const std::string& value, bool same_user = false);

	void start(const std::string& app_key, const std::string& host, int port = -1, bool start_thread = false);

	void startOnCloud(const std::string& app_key);

	void stop();

	void setUpdateInterval(size_t milliseconds);

	class Event;

	void addEvent(const Event& event);

	void setMaxEvents(size_t value);

	void flushEvents(std::chrono::seconds timeout = std::chrono::seconds(30));

	bool beginSession();

	bool updateSession();

	bool endSession();

	void enableRemoteConfig();

	void updateRemoteConfig();

	json getRemoteConfigValue(const std::string& key);

	void updateRemoteConfigFor(std::string *keys, size_t key_count);

	void updateRemoteConfigExcept(std::string *keys, size_t key_count);

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
		Event(const std::string& key, size_t count, double sum, double duration);

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

	void SetPath(const std::string& path) {
#ifdef COUNTLY_USE_SQLITE
		setDatabasePath(path);
#elif defined _WIN32
		UNREFERENCED_PARAMETER(path);
#endif
	}

	void SetMetrics(const std::string& os, const std::string& os_version, const std::string& device, const std::string& resolution, const std::string& carrier, const std::string& app_version) {
		setMetrics(os, os_version, device, resolution, carrier, app_version);
	}

	void SetMaxEventsPerMessage(int maxEvents) {
		setMaxEvents(maxEvents);
	}

	void SetMinUpdatePeriod(int minUpdateMillis) {
		setUpdateInterval(minUpdateMillis);
	}

	void Start(const std::string& appKey, const std::string& host, int port) {
		start(appKey, host, port);
	}

	void StartOnCloud(const std::string& appKey) {
		startOnCloud(appKey);
	}

	void Stop() {
		stop();
	}

	void RecordEvent(const std::string key, int count) {
		addEvent(Event(key, count));
	}

	void RecordEvent(const std::string key, int count, double sum) {
		addEvent(Event(key, count, sum));
	}

	void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count) {
		Event event(key, count);

		for (auto key_value: segmentation) {
			event.addSegmentation(key_value.first, json::parse(key_value.second));
		}

		addEvent(event);
	}

	void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count, double sum) {
		Event event(key, count, sum);

		for (auto key_value: segmentation) {
			event.addSegmentation(key_value.first, json::parse(key_value.second));
		}

		addEvent(event);
	}

	void RecordEvent(const std::string key, std::map<std::string, std::string> segmentation, int count, double sum, double duration) {
		Event event(key, count, sum, duration);

		for (auto key_value: segmentation) {
			event.addSegmentation(key_value.first, json::parse(key_value.second));
		}

		addEvent(event);
	}

	/* Provide 'updateInterval' in seconds. */
	inline void setAutomaticSessionUpdateInterval(unsigned short updateInterval) {
		_auto_session_update_interval = updateInterval;
	}
private:
	void log(LogLevel level, const std::string& message);

	HTTPResponse sendHTTP(std::string path, std::string data);

	void _changeDeviceIdWithMerge(const std::string& value);

	void _changeDeviceIdWithoutMerge(const std::string& value);

	std::chrono::system_clock::duration getSessionDuration(std::chrono::system_clock::time_point now);

	std::chrono::system_clock::duration getSessionDuration();

	void updateLoop();

	void (*logger_function)(LogLevel level, const std::string& message);
	HTTPResponse (*http_client_function)(bool is_post, const std::string& url, const std::string& data);

	std::string host;
	int port;
	bool use_https;
	bool always_use_post;
	std::chrono::system_clock::time_point last_sent_session_request;
	bool began_session;

	json session_params;
	std::string salt;

	std::thread *thread;
	std::mutex mutex;
	bool stop_thread;
	bool running;
	size_t wait_milliseconds;
	unsigned short _auto_session_update_interval = 60; // value is in seconds;

	size_t max_events;
#ifndef COUNTLY_USE_SQLITE
	std::deque<std::string> event_queue;
#else
	std::string database_path;
#endif

	bool remote_config_enabled;
	json remote_config;
};

#endif
