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

/**
 * Set limit for the number of requests that can be stored locally.
 * @param requestQueueSize: max size of request queue
 */
void Countly::setMaxRequestQueueSize(unsigned int requestQueueSize) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setMaxRequestQueueSize] You can not set the request queue size after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->requestQueueThreshold = requestQueueSize;
  mutex->unlock();
}

/**
 * Set limit for the number of requests that can be processed at a time.
 * If the limit is reached, the rest of the requests will be processed in the next cycle.
 * @param batchSize: max size of requests to process at a time
 */
void Countly::setMaxRQProcessingBatchSize(unsigned int batchSize) {
  mutex->lock();
  configuration->maxProcessingBatchSize = batchSize;
  mutex->unlock();
}

void Countly::alwaysUsePost(bool value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][alwaysUsePost] You can not set the http method after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->forcePost = value;
  mutex->unlock();
}

void Countly::setSalt(const std::string &value) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setSalt] You can not set the salt after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->salt = value;
  mutex->unlock();
}

void Countly::setLogger(void (*fun)(LogLevel level, const std::string &message)) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setLogger] You can not set the logger after SDK initialization.");
    return;
  }

  mutex->lock();
  logger->setLogger(fun);
  mutex->unlock();
}

void Countly::setHTTPClient(HTTPClientFunction fun) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setHTTPClient] You can not set the http client after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->http_client_function = fun;
  mutex->unlock();
}

void Countly::setSha256(SHA256Function fun) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setHTTPClient] You can not set the 'SHA256' function after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->sha256_function = fun;
  mutex->unlock();
}

/**
 * Enable manual session handling.
 */
void Countly::enableManualSessionControl() {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][enableManualSessionControl] You can not enable manual session control after SDK initialization.");
    return;
  }

  mutex->lock();
  configuration->manualSessionControl = true;
  mutex->unlock();
}

void Countly::setMetrics(const std::string &os, const std::string &os_version, const std::string &device, const std::string &resolution, const std::string &carrier, const std::string &app_version) {
  if (is_sdk_initialized) {
    log(LogLevel::WARNING, "[Countly][setMetrics] You can not set metrics after SDK initialization.");
    return;
  }

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
  if(!configuration->manualSessionControl){
    endSession();
  }

  mutex->lock();
  session_params["device_id"] = value;
  mutex->unlock();

  // start a new session for new user
  if(!configuration->manualSessionControl){
    beginSession();
  }
  
}
#pragma endregion Device Id

void Countly::start(const std::string &app_key, const std::string &host, int port, bool start_thread) {
  mutex->lock();
  if (is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly][start] SDK has already been initialized, 'start' should not be called a second time!");
    mutex->unlock();
    return;
  }

#ifdef COUNTLY_USE_SQLITE
  if (configuration->databasePath == "" || configuration->databasePath == " ") {
    log(LogLevel::ERROR, "[Countly][start] Database path can not be empty or blank.");
    return;
  }
#endif

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

  enable_automatic_session = start_thread && !configuration->manualSessionControl;
  start_thread = true;

  if (port < 0 || port > 65535) {
    log(LogLevel::WARNING, "[Countly][start] Port number is out of valid boundaries. Setting it to 0.");
    port = 0;
  }

  configuration->port = port;
  configuration->appKey = app_key;
  configuration->serverUrl = host;

  session_params["app_key"] = app_key;

#ifdef COUNTLY_USE_SQLITE
  storageModule.reset(new StorageModuleDB(configuration, logger));
#else
  storageModule.reset(new StorageModuleMemory(configuration, logger));
#endif
  storageModule->init();

  requestBuilder.reset(new RequestBuilder(configuration, logger));
  requestModule.reset(new RequestModule(configuration, logger, requestBuilder, storageModule));
  crash_module.reset(new cly::CrashModule(configuration, logger, requestModule, mutex));
  views_module.reset(new cly::ViewsModule(this, logger));

  bool result = true;
#ifdef COUNTLY_USE_SQLITE
  result = createEventTableSchema();
#endif

  is_sdk_initialized = result; // after this point SDK is initialized.

  if (!running) {

    if(!configuration->manualSessionControl){
      mutex->unlock();
      beginSession();
      mutex->lock();
    }

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
  if (!configuration->manualSessionControl) {
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
  event_queue.push_back(event.serialize());
#else
  addEventToSqlite(event);
#endif
  mutex->unlock();
  checkAndSendEventToRQ();
}

void Countly::checkAndSendEventToRQ() {
  nlohmann::json events = nlohmann::json::array();
  int queueSize = checkEQSize();
  mutex->lock();
#ifdef COUNTLY_USE_SQLITE
  if (queueSize >= configuration->eventQueueThreshold) {
    log(LogLevel::DEBUG, "Event queue threshold is reached");
    std::string event_ids;

    // fetch events up to the threshold from the database
    mutex->unlock();
    fillEventsIntoJson(events, event_ids);
    mutex->lock();
    // send them to request queue
    sendEventsToRQ(events);
    // remove them from database
    removeEventWithId(event_ids);
  }
#else
  if (queueSize >= configuration->eventQueueThreshold) {
    log(LogLevel::WARNING, "Event queue is full, dropping the oldest event to insert a new one");
    for (const auto &event_json : event_queue) {
      events.push_back(nlohmann::json::parse(event_json));
    }
    sendEventsToRQ(events);
    event_queue.clear();
  }
#endif
  mutex->unlock();
}

void Countly::setMaxEvents(size_t value) {
  log(LogLevel::WARNING, "[Countly][setMaxEvents/SetMaxEventsPerMessage] These calls are deprecated. Use 'setEventsToRQThreshold' instead.");
  setEventsToRQThreshold(static_cast<int>(value));
}

void Countly::setEventsToRQThreshold(int value) {
  log(LogLevel::DEBUG, "[Countly][setEventsToRQThreshold] Given threshold:[" + std::to_string(value) + "]");
  mutex->lock();
  if (value < 1) {
    log(LogLevel::WARNING, "[Countly][setEventsToRQThreshold] Threshold can not be less than 1. Setting it to 1 instead of:[" + std::to_string(value) + "]");
    value = 1;
  } else if (value > 10000) {
    log(LogLevel::WARNING, "[Countly][setEventsToRQThreshold] Threshold can not be greater than 10000. Setting it to 10000 instead of:[" + std::to_string(value) + "]");
    value = 10000;
  }

  // set the value
  configuration->eventQueueThreshold = value;
  // if current queue size is greater than the new threshold, send events to RQ
  mutex->unlock();
  checkAndSendEventToRQ();
}

void Countly::flushEvents(std::chrono::seconds timeout) {
  log(LogLevel::DEBUG, "[Countly][flushEvents] timeout: " + std::to_string(timeout.count()) + " seconds");

  try {
    auto wait_duration = std::chrono::seconds(1);
    bool update_failed;

    // Try to update session
    while (timeout.count() != 0) {

      // try to update session if event queue is not empty
      update_failed = attemptSessionUpdateEQ();

      // if update is successful or EQ is empty, break the loop
      if (!update_failed) {
        break;
      }

      // wait for a while
      std::this_thread::sleep_for(wait_duration);
      // increase wait/retry duration	(exponential backoff: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, ...)
      wait_duration *= 2;
      // if wait/retry time is bigger than timeout stop trying, else decrease timeout
      timeout = (wait_duration > timeout) ? std::chrono::seconds(0) : (timeout - wait_duration);
    }

    // Clear the event queue
    clearEQInternal(); // TODO: Check if this is necessary

    // TODO: Check if we capture anything other than a system_error
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "flushEvents, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
}

bool Countly::attemptSessionUpdateEQ() {
  // return false if event queue is empty
#ifndef COUNTLY_USE_SQLITE
  mutex->lock();
  if (event_queue.empty()) {
    mutex->unlock();
    return false;
  }
  mutex->unlock();
#else
  int event_count = checkEQSize();
  if (event_count <= 0) {
    return false;
  }
#endif
  bool result;
  if(!configuration->manualSessionControl){
    result = !updateSession();
  } else {
    log(LogLevel::WARNING, "[Countly][attemptSessionUpdateEQ] SDK is in manual session control mode. Please start a session first.");
    result = false;
  }
  
  packEvents();
  return result;
}

void Countly::clearEQInternal() {
#ifndef COUNTLY_USE_SQLITE
  event_queue.clear();
#else
  clearPersistentEQ();
#endif
}

#ifdef COUNTLY_BUILD_TESTS
std::vector<std::string> Countly::debugReturnStateOfEQ() {
  try {

#ifdef COUNTLY_USE_SQLITE
    std::vector<std::string> v;
    sqlite3 *database;
    int return_value, row_count, column_count;
    char **table;
    char *error_message;

    return_value = sqlite3_open(configuration->databasePath.c_str(), &database);
    if (return_value == SQLITE_OK) {
      std::ostringstream sql_statement_stream;
      sql_statement_stream << "SELECT * FROM events ORDER BY evtid ASC;";
      std::string sql_statement = sql_statement_stream.str();

      return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
      bool no_request = (row_count == 0);
      if (return_value == SQLITE_OK && !no_request) {

        for (int event_index = 1; event_index < row_count + 1; event_index++) {
          std::string rqstId = table[event_index * column_count];
          std::string rqst = table[(event_index * column_count) + 1];
          v.push_back(rqst);
        }

      } else if (return_value != SQLITE_OK) {
        std::string error(error_message);
        sqlite3_free(error_message);
      }
      sqlite3_free_table(table);
    }
    sqlite3_close(database);
#else
    std::vector<std::string> v(event_queue.begin(), event_queue.end());
#endif
    return v;
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "debugReturnStateOfEQ, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
}
#endif

bool Countly::beginSession() {
  mutex->lock();
  log(LogLevel::INFO, "[Countly][beginSession]");
  if (began_session) {
    mutex->unlock();
    log(LogLevel::DEBUG, "[Countly][beginSession] Session is already active.");
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

/**
 * @brief Update session
 */
bool Countly::updateSession() {
  try {
    // Check if there was a session, if not try to start one
    mutex->lock();
    if (!began_session) {
      mutex->unlock();
      if(configuration->manualSessionControl){
        log(LogLevel::WARNING, "[Countly][updateSession] SDK is in manual session control mode and there is no active session. Please start a session first.");
        return false;
      }
      if (!beginSession()) {
        // if beginSession fails, we should not try to update session
        return false;
      }
      mutex->lock();
      began_session = true;
    }
   
    mutex->unlock();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
    mutex->lock();

    // report session duration if it is greater than the configured session duration value
    if (duration.count() >= configuration->sessionDuration) {
      log(LogLevel::DEBUG, "[Countly][updateSession] sending session update.");
      std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"session_duration", std::to_string(duration.count())}};
      requestModule->addRequestToQueue(data);

      last_sent_session_request += duration;
    }
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "update session, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
  mutex->unlock();
  return true;
}

void Countly::packEvents() {
  try {
   // events array
    nlohmann::json events = nlohmann::json::array();
    std::string event_ids;
    mutex->unlock();
    bool no_events = checkEQSize() > 0 ? false : true;
    mutex->lock();

    if (!no_events) {
#ifndef COUNTLY_USE_SQLITE
      for (const auto &event_json : event_queue) {
        events.push_back(nlohmann::json::parse(event_json));
      }
#else
      // TODO: If database_path was empty there was return false here
      mutex->unlock();
      fillEventsIntoJson(events, event_ids);
      mutex->lock();
#endif
    } else {
      log(LogLevel::DEBUG, "[Countly][updateSession] EQ empty.");
    }
 // report events if there are any to request queue
    if (!no_events) {
      sendEventsToRQ(events);
    }

// clear event queue
// TODO: check if we want to totally wipe the event queue in memory but not in database
#ifndef COUNTLY_USE_SQLITE
    event_queue.clear();
#else
    if (!event_ids.empty()) {
      // this is a partial clearance, we only remove the events that were sent
      removeEventWithId(event_ids);
    }
#endif
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "update session, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
  mutex->unlock();
}


void Countly::sendEventsToRQ(const nlohmann::json &events) {
  log(LogLevel::DEBUG, "[Countly][sendEventsToRQ] Sending events to RQ.");
  std::map<std::string, std::string> data = {{"app_key", session_params["app_key"].get<std::string>()}, {"device_id", session_params["device_id"].get<std::string>()}, {"events", events.dump()}};
  requestModule->addRequestToQueue(data);
}

bool Countly::endSession() {
  log(LogLevel::INFO, "[Countly][endSession]");
  if(!began_session) {
    log(LogLevel::DEBUG, "[Countly][endSession] There is no active session to end.");
    return true;
  }
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

int Countly::checkEQSize() {
  log(LogLevel::DEBUG, "[Countly][checkEQSize]");
  int event_count = -1;
  if (!is_sdk_initialized) {
    log(LogLevel::DEBUG, "[Countly][checkEQSize] SDK is not initialized.");
    return event_count;
  }

#ifdef COUNTLY_USE_SQLITE
  event_count = checkPersistentEQSize();
#else
  event_count = checkMemoryEQSize();
#endif
  return event_count;
}

int Countly::checkRQSize() {
  log(LogLevel::DEBUG, "[Countly][checkRQSize]");
  int request_count = -1;
  if (!is_sdk_initialized) {
    log(LogLevel::DEBUG, "[Countly][checkRQSize] SDK is not initialized.");
    return request_count;
  }

  request_count = static_cast<int>(requestModule->RQSize());
  return request_count;
}

#ifndef COUNTLY_USE_SQLITE
int Countly::checkMemoryEQSize() {
  log(LogLevel::DEBUG, "[Countly][checkMemoryEQSize] Checking event queue size in memory");
  int result = 0;
  mutex->lock();
  result = static_cast<int>(event_queue.size());
  mutex->unlock();
  return result;
}
#endif

// Standalone Sqlite functions
#ifdef COUNTLY_USE_SQLITE
void Countly::removeEventWithId(const std::string &event_ids) {
  // TODO: Check if we should check database_path set or not
  log(LogLevel::DEBUG, "[Countly][removeEventWithId] Removing events from storage: " + event_ids);
  sqlite3 *database;
  int return_value;
  char *error_message;

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
    } else {
      log(LogLevel::DEBUG, "[Countly][removeEventWithId] Removed events with the given ID(s).");
    }
  } else {
    log(LogLevel::ERROR, "[Countly][removeEventWithId] Could not open database.");
  }
  sqlite3_close(database);
}

void Countly::fillEventsIntoJson(nlohmann::json &events, std::string &event_ids) {
  mutex->lock();
  if (database_path.empty()) {
    mutex->unlock();
    log(LogLevel::FATAL, "[Countly][fillEventsIntoJson] Sqlite database path is not set.");
    event_ids = "";
    return;
  }

  log(LogLevel::DEBUG, "[Countly][fillEventsIntoJson] Fetching events from storage.");
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  // open database
  return_value = sqlite3_open(database_path.c_str(), &database);
  // if database opened successfully
  if (return_value == SQLITE_OK) {

    // create sql statement to fetch events as much as the event queue threshold
    // TODO: check if this is something we want to do
    std::string sql_statement = "SELECT evtid, event FROM events;";

    // execute sql statement
    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      std::ostringstream event_id_stream;
      event_id_stream << '(';

      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        event_id_stream << table[event_index * column_count] << ',';
        // add event to the events array
        events.push_back(nlohmann::json::parse(table[(event_index * column_count) + 1]));
      }

      log(LogLevel::DEBUG, "[Countly][fillEventsIntoJson] Events count = " + std::to_string(events.size()));

      event_id_stream.seekp(-1, event_id_stream.cur);
      event_id_stream << ')';

      // write event ids to a string stream (or more like copy out that stream here) to be used in the delete statement
      event_ids = event_id_stream.str();
    } else {
      log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  } else {
    log(LogLevel::ERROR, "[Countly][fillEventsIntoJson] Could not open database.");
  }
  sqlite3_close(database);
  mutex->unlock();
}

int Countly::checkPersistentEQSize() {
  int result = -1;
  mutex->lock();
  if (database_path.empty()) {
    mutex->unlock();
    log(LogLevel::FATAL, "[Countly][checkEQSize] Sqlite database path is not set");
    return result;
  }

  sqlite3 *database;
  int return_value = sqlite3_open(database_path.c_str(), &database);
  mutex->unlock();

  if (return_value == SQLITE_OK) {
    char *error_message;
    int row_count, column_count;
    char **table;
    return_value = sqlite3_get_table(database, "SELECT COUNT(*) FROM events;", &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      result = atoi(table[1]);
      log(LogLevel::DEBUG, "[Countly][checkEQSize] Fetched event count from database: " + std::to_string(result));
    } else {
      log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  } else {
    log(LogLevel::WARNING, "[Countly][checkEQSize] Could not open database");
  }
  sqlite3_close(database);
  return result;
}

void Countly::addEventToSqlite(const cly::Event &event) {
  log(LogLevel::DEBUG, "[Countly][addEventToSqlite]");
  try {
    if (database_path.empty()) {
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
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "addEventToSqlite, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
}

void Countly::clearPersistentEQ() {
  log(LogLevel::DEBUG, "[Countly][clearEQ]");
  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "DELETE FROM events;", nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      log(LogLevel::FATAL, error_message);
      sqlite3_free(error_message);
    } else {
      log(LogLevel::DEBUG, "[Countly][clearEQ] Cleared event queue");
    }
  }
  sqlite3_close(database);
}

void Countly::setDatabasePath(const std::string &path) {
  if (is_sdk_initialized) {
    log(LogLevel::ERROR, "[Countly][setDatabasePath] You can not set the database path after SDK initialization.");
    return;
  }

  if (path == "" || path == " ") {
    log(LogLevel::ERROR, "[Countly][setDatabasePath] Database path can not be empty or blank.");
    return;
  }

  configuration->databasePath = path;
  log(LogLevel::INFO, "[Countly][setDatabasePath] path = " + path);
}

bool Countly::createEventTableSchema() {
  try {
    bool result = false;
    sqlite3 *database;
    int return_value, row_count, column_count;
    char **table;
    char *error_message;

    database_path = configuration->databasePath;

    return_value = sqlite3_open(database_path.c_str(), &database);
    if (return_value == SQLITE_OK) {
      return_value = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS events (evtid INTEGER PRIMARY KEY, event TEXT)", nullptr, nullptr, &error_message);
      if (return_value != SQLITE_OK) {
        log(LogLevel::ERROR, error_message);
        sqlite3_free(error_message);
      } else {
        result = true;
      }
    } else {
      const char *error = sqlite3_errmsg(database);
      log(LogLevel::ERROR, "[Countly][createEventTableSchema] " + std::string(error));
      database_path.clear();
    }
    sqlite3_close(database);
    return result;
  } catch (const std::system_error &e) {
    std::ostringstream log_message;
    log_message << "createEventTableSchema, error: " << e.what();
    log(LogLevel::FATAL, log_message.str());
  }
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
    packEvents();
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
