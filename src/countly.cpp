#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#ifndef COUNTLY_USE_CUSTOM_SHA256
#include "openssl/sha.h"
#endif

#include "countly.hpp"

#ifdef COUNTLY_USE_SQLITE
#include "sqlite3.h"
#endif

namespace cly {
Countly::Countly() {
  crash_module = nullptr;
  views_module = nullptr;
  logger.reset(new cly::LoggerModule());
  configuration.reset(new cly::CountlyConfiguration("", ""));
}

Countly::~Countly() {
  is_being_disposed = true;
  stop();
  crash_module.reset();
  views_module.reset();
  logger.reset();
}

std::unique_ptr<Countly> _sharedInstance;
Countly &Countly::getInstance() {
  if (_sharedInstance.get() == nullptr) {
    _sharedInstance.reset(new Countly());
  }
  return *_sharedInstance.get();
}

#ifdef COUNTLY_BUILD_TESTS
void Countly::halt() { _sharedInstance.reset(new Countly()); }
#endif

void Countly::alwaysUsePost(bool value) {
  mutex->lock();
  configuration->forcePost = value;
  mutex->unlock();
}

void Countly::setSalt(const std::string &value) {
  mutex->lock();
  configuration->salt = value;
  mutex->unlock();
}

void Countly::setLogger(void (*fun)(LogLevel level, const std::string &message)) {
  mutex->lock();
  logger->setLogger(fun);
  mutex->unlock();
}

void Countly::setHTTPClient(HTTPClientFunction fun) {
  mutex->lock();
  configuration->http_client_function = fun;
  mutex->unlock();
}

void Countly::setSha256(SHA256Function fun) {
  mutex->lock();
  configuration->sha256_function = fun;
  mutex->unlock();
}

void Countly::setMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version) {
  if (!os.empty()) {
    configuration->metrics["_os"] = os;
  }
  if (!os_version.empty()) {
    configuration->metrics["_os_version"] = os_version;
  }

  if (!device.empty()) {
    configuration->metrics["_device"] = device;
  }

  if (!resolution.empty()) {
    configuration->metrics["_resolution"] = resolution;
  }

  if (!carrier.empty()) {
    configuration->metrics["_carrier"] = carrier;
  }

  if (!app_version.empty()) {
    configuration->metrics["_app_version"] = app_version;
  }
}

void Countly::setUserDetails(const std::map<std::string, std::string> &value) {
  mutex->lock();
  session_params["user_details"] = value;

  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly][setUserDetails] Can not send user detail if the SDK has not been initialized.");
    mutex->unlock();
    return;
  }

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"user_details", session_params["user_details"].dump()}};

  requestModule->addRequestToQueue(data);
  mutex->unlock();
}

void Countly::setCustomUserDetails(const std::map<std::string, std::string> &value) {
  mutex->lock();
  session_params["user_details"]["custom"] = value;

  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly][setCustomUserDetails] Can not send user detail if the SDK has not been initialized.");
    mutex->unlock();
    return;
  }

  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"user_details", session_params["user_details"].dump()}};
  requestModule->addRequestToQueue(data);

  mutex->unlock();
}

#pragma region User location

void Countly::setCountry(const std::string &country_code) {
  log(LogLevel::WARNING, "[Countly][setCountry] 'setCountry' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  setLocation(country_code, "", "", "");
}

void Countly::setCity(const std::string &city_name) {
  log(LogLevel::WARNING, "[Countly][setCity] 'setCity' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");
  setLocation("", city_name, "", "");
}

void Countly::setLocation(double lattitude, double longitude) {
  log(LogLevel::WARNING, "[Countly][setLocation] 'setLocation(latitude, longitude)' is deprecated, please use 'setLocation(countryCode, city, gpsCoordinates, ipAddress)' method instead.");

  std::ostringstream location_stream;
  location_stream << lattitude << ',' << longitude;
  setLocation("", "", location_stream.str(), "");
}

void Countly::setLocation(const std::string &countryCode, const std::string &city, const std::string &gpsCoordinates, const std::string &ipAddress) {
  mutex->lock();
  log(LogLevel::INFO, "[Countly][setLocation] SetLocation : countryCode = " + countryCode + ", city = " + city + ", gpsCoordinates = " + gpsCoordinates + ", ipAddress = " + ipAddress);

  if ((!countryCode.empty() && city.empty()) || (!city.empty() && countryCode.empty())) {
    log(LogLevel::WARNING, "[Countly][setLocation] In \"SetLocation\" both country code and city should be set together");
  }

  session_params["city"] = city;
  session_params["ip_address"] = ipAddress;
  session_params["location"] = gpsCoordinates;
  session_params["country_code"] = countryCode;

  mutex->unlock();

  if (is_sdk_initialized) {
    _sendIndependantLocationRequest();
  }
}

void Countly::_sendIndependantLocationRequest() {
  mutex->lock();
  log(LogLevel::DEBUG, "[Countly] [_sendIndependantLocationRequest]");

  /*
   * Empty country code, city and IP address can not be sent.
   */

  std::map<std::string, std::string> data;

  if (session_params.contains("city") && session_params["city"].is_string() && !session_params["city"].get<std::string>().empty()) {
    data["city"] = session_params["city"].get<std::string>();
  }

  if (session_params.contains("location") && session_params["location"].is_string() && !session_params["location"].get<std::string>().empty()) {
    data["location"] = session_params["location"].get<std::string>();
  }

  if (session_params.contains("country_code") && session_params["country_code"].is_string() && !session_params["country_code"].get<std::string>().empty()) {
    data["country_code"] = session_params["country_code"].get<std::string>();
  }

  if (session_params.contains("ip_address") && session_params["ip_address"].is_string() && !session_params["ip_address"].get<std::string>().empty()) {
    data["ip_address"] = session_params["ip_address"].get<std::string>();
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  if (!data.empty()) {
    data["app_key"] = session_params["app_key"].get<std::string>();
    data["device_id"] = session_params["device_id"].get<std::string>();
    data["timestamp"] = std::to_string(timestamp.count());
    requestModule->addRequestToQueue(data);
  }

  mutex->unlock();
}

#pragma endregion User location

#pragma region Device Id
void Countly::setDeviceID(const std::string &value, bool same_user) {
  mutex->lock();
  configuration->deviceId = value;
  log(LogLevel::INFO, "[Countly][changeDeviceIdWithMerge] setDeviceID = '" + value + "'");

  // Checking old and new devices ids are same
  if (session_params.contains("device_id") && session_params["device_id"].get<std::string>() == value) {
    log(LogLevel::DEBUG, "[Countly][setDeviceID] new device id and old device id are same.");
    mutex->unlock();
    return;
  }

  if (!session_params.contains("device_id")) {
    session_params["device_id"] = value;
    log(LogLevel::DEBUG, "[Countly][setDeviceID] no device was set, setting device id");
    mutex->unlock();
    return;
  }

  mutex->unlock();
  if (!is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly][setDeviceID] Can not change the device id if the SDK has not been initialized.");
    return;
  }

  if (same_user) {
    _changeDeviceIdWithMerge(value);
  } else {
    _changeDeviceIdWithoutMerge(value);
  }
}

/* Change device ID with merge after SDK has been initialized.*/
void Countly::_changeDeviceIdWithMerge(const std::string &value) {
  mutex->lock();
  log(LogLevel::DEBUG, "[Countly][changeDeviceIdWithMerge] deviceId = '" + value + "'");

  session_params["old_device_id"] = session_params["device_id"];
  session_params["device_id"] = value;

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  std::map<std::string, std::string> data = {
      {"app_key", session_params["app_key"].get<std::string>()},
      {"device_id", session_params["device_id"].get<std::string>()},
      {"old_device_id", session_params["old_device_id"].get<std::string>()},
      {"timestamp", std::to_string(timestamp.count())},
  };
  requestModule->addRequestToQueue(data);

  session_params.erase("old_device_id");
  mutex->unlock();
}

/* Change device ID without merge after SDK has been initialized.*/
void Countly::_changeDeviceIdWithoutMerge(const std::string &value) {
  log(LogLevel::DEBUG, "[Countly][changeDeviceIdWithoutMerge] deviceId = '" + value + "'");

  // send all event to server and end current session of old user
  flushEvents();
  if (began_session) {
    endSession();
    mutex->lock();
    session_params["device_id"] = value;
    mutex->unlock();
    beginSession();
  } else {
    mutex->lock();
    session_params["device_id"] = value;
    mutex->unlock();
  }
}
#pragma endregion Device Id

void Countly::start(const std::string &app_key, const std::string &host, int port, bool start_thread) {
  mutex->lock();
  log(LogLevel::INFO, "[Countly][start]");

#ifdef COUNTLY_USE_SQLITE
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_SQLITE' is defined");
#else
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_SQLITE' is not defined");
#endif

#ifdef COUNTLY_USE_CUSTOM_HTTP
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_CUSTOM_HTTP' is defined");
#else
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_CUSTOM_HTTP' is not defined");
#endif

#ifdef COUNTLY_USE_CUSTOM_SHA256
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_CUSTOM_SHA256' is defined");
#else
  log(LogLevel::INFO, "[Countly][start] 'COUNTLY_USE_CUSTOM_SHA256' is not defined");
#endif

#ifdef _WIN32
  log(LogLevel::INFO, "[Countly][start] '_WIN32' is defined");
#else
  log(LogLevel::INFO, "[Countly][start] '_WIN32' is not defined");
#endif

  enable_automatic_session = start_thread;
  start_thread = true;

  configuration->port = port;
  configuration->appKey = app_key;
  configuration->serverUrl = host;

  session_params["app_key"] = app_key;

#ifdef COUNTLY_USE_SQLITE
  // TODO storageModule.reset(new StorageModuleDB(configuration, logger));
  storageModule.reset(new StorageModuleMemory(configuration, logger));
#else
  storageModule.reset(new StorageModuleMemory(configuration, logger));
#endif
  storageModule->init();

  requestBuilder.reset(new RequestBuilder(configuration, logger));
  requestModule.reset(new RequestModule(configuration, logger, requestBuilder, storageModule));
  crash_module.reset(new cly::CrashModule(configuration, logger, requestModule, mutex));
  views_module.reset(new cly::ViewsModule(this, logger));

  is_sdk_initialized = true; // after this point SDK is initialized.

  if (!running) {

    mutex->unlock();
    beginSession();
    mutex->lock();

    if (start_thread) {
      stop_thread = false;

      try {
        thread.reset(new std::thread(&Countly::updateLoop, this));
      } catch (const std::system_error &e) {
        std::ostringstream log_message;
        log_message << "Could not create thread: " << e.what();
        log(LogLevel::FATAL, log_message.str());
      }
    }
  }
  mutex->unlock();
}

/**
 * startOnCloud is deprecated and this is going to be removed in the future.
 */
void Countly::startOnCloud(const std::string &app_key) {
  log(LogLevel::WARNING, "[Countly][startOnCloud] 'startOnCloud' is deprecated, this is going to be removed in the future.");
  this->start(app_key, "https://cloud.count.ly", 443);
}

void Countly::stop() {
  _deleteThread();
  if (began_session) {
    endSession();
  }
}

void Countly::_deleteThread() {
  mutex->lock();
  stop_thread = true;
  mutex->unlock();
  if (thread && thread->joinable()) {
    try {
      thread->join();
    } catch (const std::system_error &e) {
      log(LogLevel::WARNING, "Could not join thread");
    }
    thread.reset();
  }
}

void Countly::setUpdateInterval(size_t milliseconds) {
  mutex->lock();
  wait_milliseconds = milliseconds;
  mutex->unlock();
}

void Countly::addEvent(const cly::Event &event) {
  mutex->lock();
#ifndef COUNTLY_USE_SQLITE
  if (event_queue.size() == configuration->eventQueueThreshold) {
    log(LogLevel::WARNING, "Event queue is full, dropping the oldest event to insert a new one");
    event_queue.pop_front();
  }
  event_queue.push_back(event.serialize());
#else
  addEventToSqlite(event);
#endif
  mutex->unlock();
}
#ifdef COUNTLY_USE_SQLITE
void Countly::addEventToSqlite(const cly::Event &event) {
  log(LogLevel::DEBUG, "[Countly][addEventToSqlite] event = " + event.serialize());

  if (database_path.empty()) {
    mutex->unlock();
    log(LogLevel::FATAL, "Cannot add event, sqlite database path is not set");
    return;
  }

  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    // TODO Investigate if we need to escape single quotes in serialized event
    sql_statement_stream << "INSERT INTO events (event) VALUES('" << event.serialize() << "');";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
}
#endif

void Countly::setMaxEvents(size_t value) {
  mutex->lock();
  configuration->eventQueueThreshold = value;
#ifndef COUNTLY_USE_SQLITE
  if (event_queue.size() > configuration->eventQueueThreshold) {
    log(LogLevel::WARNING, "New event queue size is smaller than the old one, dropping the oldest events to fit");
    event_queue.resize(configuration->eventQueueThreshold);
  }
#endif
  mutex->unlock();
}

void Countly::flushEvents(std::chrono::seconds timeout) {
  auto wait_duration = std::chrono::seconds(1);
  bool update_failed;
  while (timeout.count() != 0) {
#ifndef COUNTLY_USE_SQLITE
    mutex->lock();
    if (event_queue.empty()) {
      mutex->unlock();
      break;
    }
    mutex->unlock();

    update_failed = !updateSession();
#else
    mutex->lock();
    if (database_path.empty()) {
      mutex->unlock();
      log(LogLevel::FATAL, "Cannot flush events, sqlite database path is not set");
      return;
    }

    sqlite3 *database;
    int return_value, row_count, column_count;
    char **table;
    char *error_message;

    update_failed = true;
    return_value = sqlite3_open(database_path.c_str(), &database);
    mutex->unlock();
    if (return_value == SQLITE_OK) {
      return_value = sqlite3_get_table(database, "SELECT COUNT(*) FROM events;", &table, &row_count, &column_count, &error_message);
      if (return_value == SQLITE_OK) {
        int event_count = atoi(table[1]);
        if (event_count > 0) {
          update_failed = !updateSession();
        }
      } else {
        log(LogLevel::ERROR, error_message);
        sqlite3_free(error_message);
      }
      sqlite3_free_table(table);
    }
    sqlite3_close(database);
#endif
    if (update_failed) {
      std::this_thread::sleep_for(wait_duration);
      wait_duration *= 2;
      timeout = (wait_duration > timeout) ? std::chrono::seconds(0) : (timeout - wait_duration);
    }
  }

#ifndef COUNTLY_USE_SQLITE
  event_queue.clear();
#else
  sqlite3 *database;
  int return_value;
  char *error_message;

  update_failed = true;
  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "DELETE FROM events;", nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::FATAL, error_message);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
#endif
}

bool Countly::beginSession() {
  mutex->lock();
  log(LogLevel::INFO, "[Countly][beginSession]");
  if (began_session) {
    mutex->unlock();
    return true;
  }

  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

  std::map<std::string, std::string> data = {{"sdk_name", COUNTLY_SDK_NAME}, {"sdk_version", COUNTLY_SDK_VERSION}, {"timestamp", std::to_string(timestamp.count())}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()},
                                             {"begin_session", "1"}};

  if (session_params.contains("city") && session_params["city"].is_string() && !session_params["city"].get<std::string>().empty()) {
    data["city"] = session_params["city"].get<std::string>();
  }

  if (session_params.contains("location") && session_params["location"].is_string() && !session_params["location"].get<std::string>().empty()) {
    data["location"] = session_params["location"].get<std::string>();
  }

  if (session_params.contains("country_code") && session_params["country_code"].is_string() && !session_params["country_code"].get<std::string>().empty()) {
    data["country_code"] = session_params["country_code"].get<std::string>();
  }

  if (session_params.contains("ip_address") && session_params["ip_address"].is_string() && !session_params["ip_address"].get<std::string>().empty()) {
    data["ip_address"] = session_params["ip_address"].get<std::string>();
  }

  if (session_params.contains("user_details")) {
    data["user_details"] = session_params["user_details"].dump();
    session_params.erase("user_details");
  }

  if (configuration->metrics.size() > 0) {
    data["metrics"] = configuration->metrics.dump();
  }

  requestModule->addRequestToQueue(data);
  session_params.erase("user_details");
  last_sent_session_request = Countly::getTimestamp();
  began_session = true;
  mutex->unlock();

  if (remote_config_enabled) {
    updateRemoteConfig();
  }
  return began_session;
}

bool Countly::updateSession() {
  mutex->lock();
  if (!began_session) {
    mutex->unlock();
    if (!beginSession()) {
      return false;
    }

    mutex->lock();
    began_session = true;
  }

  nlohmann::json events = nlohmann::json::array();
  bool no_events;

#ifndef COUNTLY_USE_SQLITE
  no_events = event_queue.empty();
  if (!no_events) {
    for (const auto &event_json : event_queue) {
      events.push_back(nlohmann::json::parse(event_json));
    }
  }
#else
  if (database_path.empty()) {
    mutex->unlock();
    log(LogLevel::FATAL, "Cannot fetch events, sqlite database path is not set");
    return false;
  }

  log(LogLevel::DEBUG, "[Countly][updateSession] fetching events from storage.");
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;
  std::string event_ids;

  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT evtid, event FROM events LIMIT " << std::dec << configuration->eventQueueThreshold << ';';
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    no_events = (row_count == 0);
    if (return_value == SQLITE_OK && !no_events) {
      std::ostringstream event_id_stream;
      event_id_stream << '(';

      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        event_id_stream << table[event_index * column_count] << ',';
        events.push_back(nlohmann::json::parse(table[(event_index * column_count) + 1]));
      }

      log(LogLevel::DEBUG, "[Countly][updateSession] events count = " + events.size());

      event_id_stream.seekp(-1, event_id_stream.cur);
      event_id_stream << ')';
      event_ids = event_id_stream.str();
    } else if (return_value != SQLITE_OK) {
      log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  }
  sqlite3_close(database);
#endif
  mutex->unlock();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
  mutex->lock();

  if (duration.count() >= configuration->sessionDuration) {
    log(LogLevel::DEBUG, "[Countly][updateSession] sending session update.");
    std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}};
    requestModule->addRequestToQueue(data);

    last_sent_session_request += duration;
  }

  if (!no_events) {
    log(LogLevel::DEBUG, "[Countly][updateSession] sending event.");
    std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"events", events.dump()}};

    requestModule->addRequestToQueue(data);
  }

#ifndef COUNTLY_USE_SQLITE
  event_queue.clear();
#else
  if (!event_ids.empty()) {
    log(LogLevel::DEBUG, "[Countly][updateSession] Removing events from storage: " + event_ids);
    // we attempt to clear the events in the database only if there were any events collected previously
    return_value = sqlite3_open(database_path.c_str(), &database);
    if (return_value == SQLITE_OK) {
      std::ostringstream sql_statement_stream;
      sql_statement_stream << "DELETE FROM events WHERE evtid IN " << event_ids << ';';
      std::string sql_statement = sql_statement_stream.str();

      return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
      if (return_value != SQLITE_OK) {
        log(LogLevel::ERROR, error_message);
        sqlite3_free(error_message);
      }
    }
    sqlite3_close(database);
  }
#endif

  mutex->unlock();
  return true;
}

bool Countly::endSession() {
  log(LogLevel::INFO, "[Countly][endSession]");
  const std::chrono::system_clock::time_point now = Countly::getTimestamp();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  const auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration(now));

  mutex->lock();
  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}, {"timestamp", std::to_string(timestamp.count())}, {"end_session", "1"}};

  if (is_being_disposed) {
    // if SDK is being destroyed, don't attempt to send the end-session request.
    mutex->unlock();
    return false;
  }

  requestModule->addRequestToQueue(data);

  last_sent_session_request = now;
  began_session = false;
  mutex->unlock();
  return true;
}

std::chrono::system_clock::time_point Countly::getTimestamp() { return std::chrono::system_clock::now(); }

#ifdef COUNTLY_USE_SQLITE
void Countly::setDatabasePath(const std::string &path) {
  log(LogLevel::INFO, "[Countly][setDatabasePath] path = " + path);

  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  mutex->lock();
  database_path = path;

  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS events (evtid INTEGER PRIMARY KEY, event TEXT)", nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
  } else {
    log(LogLevel::ERROR, "Failed to open sqlite database");
    sqlite3_free(error_message);
    database_path.clear();
  }
  sqlite3_close(database);
  mutex->unlock();
}
#endif

void Countly::log(LogLevel level, const std::string &message) { logger->log(cly::LogLevel(level), message); }

static size_t countly_curl_write_callback(void *data, size_t byte_size, size_t n_bytes, std::string *body) {
  size_t data_size = byte_size * n_bytes;
  body->append((const char *)data, data_size);
  return data_size;
}

std::string Countly::calculateChecksum(const std::string &salt, const std::string &data) {
  std::string salted_data = data + salt;
#ifdef COUNTLY_USE_CUSTOM_SHA256
  if (configuration->sha256_function == nullptr) {
    log(LogLevel::FATAL, "Missing SHA 256 function");
    return {};
  }

  return configuration->sha256_function(salted_data);
#else
  unsigned char checksum[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;

  SHA256_Init(&sha256);
  SHA256_Update(&sha256, salted_data.c_str(), salted_data.size());
  SHA256_Final(checksum, &sha256);

  std::ostringstream checksum_stream;
  for (size_t index = 0; index < SHA256_DIGEST_LENGTH; index++) {
    checksum_stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(checksum[index]);
  }

  return checksum_stream.str();
#endif
}

std::chrono::system_clock::duration Countly::getSessionDuration(std::chrono::system_clock::time_point now) {
  mutex->lock();
  std::chrono::system_clock::duration duration = now - last_sent_session_request;
  mutex->unlock();
  return duration;
}

std::chrono::system_clock::duration Countly::getSessionDuration() { return Countly::getSessionDuration(Countly::getTimestamp()); }

void Countly::updateLoop() {
  log(LogLevel::DEBUG, "[Countly][updateLoop]");
  mutex->lock();
  running = true;
  mutex->unlock();
  while (true) {
    mutex->lock();
    if (stop_thread) {
      stop_thread = false;
      mutex->unlock();
      break;
    }
    size_t last_wait_milliseconds = wait_milliseconds;
    mutex->unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(last_wait_milliseconds));
    if (enable_automatic_session) {
      updateSession();
    }
    requestModule->processQueue(mutex);
  }
  mutex->lock();
  running = false;
  mutex->unlock();
}

void Countly::enableRemoteConfig() {
  mutex->lock();
  remote_config_enabled = true;
  mutex->unlock();
}

void Countly::_fetchRemoteConfig(const std::map<std::string, std::string> &data) {
  HTTPResponse response = requestModule->sendHTTP("/o/sdk", requestBuilder->serializeData(data));
  mutex->lock();
  if (response.success) {
    remote_config = response.data;
  }
  mutex->unlock();
}

void Countly::updateRemoteConfig() {
  mutex->lock();
  if (!session_params["app_key"].is_string() || !session_params["device_id"].is_string()) {

    log(LogLevel::ERROR, "Error updating remote config, app key or device id is missing");
    mutex->unlock();
    return;
  }
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  mutex->unlock();

  // Fetch remote config asynchronously
  std::thread _thread(&Countly::_fetchRemoteConfig, this, data);
  _thread.detach();
}

nlohmann::json Countly::getRemoteConfigValue(const std::string &key) {
  mutex->lock();
  nlohmann::json value = remote_config[key];
  mutex->unlock();
  return value;
}

void Countly::_updateRemoteConfigWithSpecificValues(const std::map<std::string, std::string> &data) {
  HTTPResponse response = requestModule->sendHTTP("/o/sdk", requestBuilder->serializeData(data));
  mutex->lock();
  if (response.success) {
    for (auto it = response.data.begin(); it != response.data.end(); ++it) {
      remote_config[it.key()] = it.value();
    }
  }
  mutex->unlock();
}

void Countly::updateRemoteConfigFor(std::string *keys, size_t key_count) {
  mutex->lock();
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  {
    nlohmann::json keys_json = nlohmann::json::array();
    for (size_t key_index = 0; key_index < key_count; key_index++) {
      keys_json.push_back(keys[key_index]);
    }
    data["keys"] = keys_json.dump();
  }
  mutex->unlock();

  // Fetch remote config asynchronously
  std::thread _thread(&Countly::_updateRemoteConfigWithSpecificValues, this, data);
  _thread.detach();
}

void Countly::updateRemoteConfigExcept(std::string *keys, size_t key_count) {
  mutex->lock();
  std::map<std::string, std::string> data = {{"method", "fetch_remote_config"}, {"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}};

  {
    nlohmann::json keys_json = nlohmann::json::array();
    for (size_t key_index = 0; key_index < key_count; key_index++) {
      keys_json.push_back(keys[key_index]);
    }
    data["omit_keys"] = keys_json.dump();
  }
  mutex->unlock();

  // Fetch remote config asynchronously
  std::thread _thread(&Countly::_updateRemoteConfigWithSpecificValues, this, data);
  _thread.detach();
}
} // namespace cly