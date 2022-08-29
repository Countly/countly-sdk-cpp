#ifndef COUNTLY_HPP_
#define COUNTLY_HPP_

#include "countly/constants.hpp"

#include <chrono>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifndef COUNTLY_USE_SQLITE
#include <deque>
#endif

#include "nlohmann/json.hpp"

#ifdef _WIN32
#undef ERROR
#endif
#include "countly/logger_module.hpp"
#include "countly/views_module.hpp"

class Countly : public cly::CountlyDelegates {
public:
  Countly();

  ~Countly();

  static Countly &getInstance();

  // Do not implicitly generate the copy constructor, this is a singleton.
  Countly(const Countly &) = delete;

  // Do not implicitly generate the copy assignment operator, this is a singleton.
  void operator=(const Countly &) = delete;

  void alwaysUsePost(bool value);

  void setSalt(const std::string &value);

  enum LogLevel { DEBUG = 1, INFO = 2, WARNING = 3, ERROR = 4, FATAL = 5 };

  void setLogger(void (*fun)(LogLevel level, const std::string &message));

  /*
  This function should not be used as it will be removed in a future release. It is
  currently added as a temporary workaround.
  */
  inline std::function<void(LogLevel, const std::string &)> getLogger() { return logger_function; }

  struct HTTPResponse {
    bool success;
    nlohmann::json data;
  };

  void setSha256(cly::SHA256Function fun);

  using HTTPClientFunction = std::function<HTTPResponse(bool, const std::string &, const std::string &)>;
  void setHTTPClient(HTTPClientFunction fun);

  void setMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version);

  void setUserDetails(const std::map<std::string, std::string> &value);

  void setCustomUserDetails(const std::map<std::string, std::string> &value);

  /*
  setCountry is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setCountry(const std::string &country_code);

  /*
  setCity is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setCity(const std::string &city_name);

  /*
  setLocation is deprecated, please use void `setLocation(const std::string& countryCode, const std::string& city, const std::string& gpsCoordinates, const std::string& ipAddress)` function instead.
  */
  void setLocation(double lattitude, double longitude);

  /*
  Set Country code(ISO Country code), City, Location and IP address to be used for future requests.
  @param[in] countryCode: ISO Country code for the user's country
  @param[in] city: Name of the user's city.
  @param[in] gpsCoordinates: Comma separate latitude and longitude values.For example, `56.42345,123.45325`.
  @param[in] ipAddress: IpAddress like `192.168.88.33`
  */
  void setLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress);

  void setDeviceID(const std::string &value, bool same_user = false);

  void start(const std::string &app_key, const std::string &host, int port = -1, bool start_thread = false);

  void startOnCloud(const std::string &app_key);

  void stop();

  void setUpdateInterval(size_t milliseconds);

  class Event;

  void addEvent(const Event &event);

  void setMaxEvents(size_t value);

  void flushEvents(std::chrono::seconds timeout = std::chrono::seconds(30));

  bool beginSession();

  bool updateSession();

  bool endSession();

  void enableRemoteConfig();

  void updateRemoteConfig();

  nlohmann::json getRemoteConfigValue(const std::string &key);

  void updateRemoteConfigFor(std::string *keys, size_t key_count);

  void updateRemoteConfigExcept(std::string *keys, size_t key_count);

  static std::chrono::system_clock::time_point getTimestamp();

  static std::string encodeURL(const std::string &data);

  static std::string serializeForm(const std::map<std::string, std::string> data);

  std::string calculateChecksum(const std::string &salt, const std::string &data);

#ifdef COUNTLY_USE_SQLITE
  void setDatabasePath(const std::string &path);
#endif

  class Event {
  public:
    Event(const std::string &key, size_t count = 1);
    Event(const std::string &key, size_t count, double sum);
    Event(const std::string &key, size_t count, double sum, double duration);

    void setTimestamp();

    void startTimer();
    void stopTimer();

    template <typename T> void addSegmentation(const std::string &key, T value) {
      if (object.find("segmentation") == object.end()) {
        object["segmentation"] = nlohmann::json::object();
      }

      object["segmentation"][key] = value;
    }

    std::string serialize() const;

  private:
    nlohmann::json object;
    bool timer_running;
    std::chrono::system_clock::time_point timestamp;
  };

  void SetPath(const std::string &path) {
#ifdef COUNTLY_USE_SQLITE
    setDatabasePath(path);
#elif defined _WIN32
    UNREFERENCED_PARAMETER(path);
#endif
  }

  void SetMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version) { setMetrics(os, os_version, device, resolution, carrier, app_version); }

  void SetMaxEventsPerMessage(int maxEvents) { setMaxEvents(maxEvents); }

  void SetMinUpdatePeriod(int minUpdateMillis) { setUpdateInterval(minUpdateMillis); }

  void Start(const std::string &appKey, const std::string &host, int port) { start(appKey, host, port); }

  void StartOnCloud(const std::string &appKey) { startOnCloud(appKey); }

  void Stop() { stop(); }

  inline cly::ViewsModule &views() const { return *views_module.get(); }

  void RecordEvent(const std::string &key, int count) override { addEvent(Event(key, count)); }

  void RecordEvent(const std::string &key, int count, double sum) override { addEvent(Event(key, count, sum)); }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count) override {
    Event event(key, count);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum) override {
    Event event(key, count, sum);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  void RecordEvent(const std::string &key, const std::map<std::string, std::string> &segmentation, int count, double sum, double duration) override {
    Event event(key, count, sum, duration);

    for (auto key_value : segmentation) {
      event.addSegmentation(key_value.first, key_value.second);
    }

    addEvent(event);
  }

  /* Provide 'updateInterval' in seconds. */
  inline void setAutomaticSessionUpdateInterval(unsigned short updateInterval) { _auto_session_update_interval = updateInterval; }

  /**
   * Convert event queue into list.
   * Warning: This method is for debugging purposes, and it is going to be removed in the future.
   * You should not be using this method.
   * @return a vector object containing events.
   */
  const std::vector<std::string> debugReturnStateOfEQ() {
    std::vector<std::string> v(event_queue.begin(), event_queue.end());
    return v;
  }

private:
  void _deleteThread();
  void _sendIndependantLocationRequest();
  void log(LogLevel level, const std::string &message);

  HTTPResponse sendHTTP(std::string path, std::string data);

  void _changeDeviceIdWithMerge(const std::string &value);

  void _changeDeviceIdWithoutMerge(const std::string &value);

  std::chrono::system_clock::duration getSessionDuration(std::chrono::system_clock::time_point now);

  std::chrono::system_clock::duration getSessionDuration();

  void updateLoop();

  cly::SHA256Function sha256_function;
  HTTPClientFunction http_client_function;
  void (*logger_function)(LogLevel level, const std::string &message) = nullptr;

  std::string host;

  int port = 0;
  bool use_https = false;
  bool always_use_post = false;

  bool began_session = false;
  bool is_being_disposed = false;
  bool is_sdk_initialized = false;

  std::chrono::system_clock::time_point last_sent_session_request;

  nlohmann::json session_params;
  std::string salt;

  std::unique_ptr<std::thread> thread;
  std::unique_ptr<cly::ViewsModule> views_module;
  std::shared_ptr<cly::LoggerModule> logger;

  std::mutex mutex;
  bool stop_thread = false;
  bool running = false;
  size_t wait_milliseconds = COUNTLY_KEEPALIVE_INTERVAL;
  unsigned short _auto_session_update_interval = 60; // value is in seconds;

  size_t max_events = COUNTLY_MAX_EVENTS_DEFAULT;
#ifndef COUNTLY_USE_SQLITE
  std::deque<std::string> event_queue;
#else
  std::string database_path;
#endif

  bool remote_config_enabled = false;
  nlohmann::json remote_config;
};

#endif
