#ifndef STORAGE_MODULE_BASE_HPP_
#define STORAGE_MODULE_BASE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <string>
#include <vector>

namespace cly {

class DataEntry {
private:
  long long _id;
  std::string _data;

public:
  DataEntry(const long long id, const std::string &data) {
    this->_id = id;
    this->_data = data;
  }

  ~DataEntry() {}

  /**
   * @return id of data entry
   */
  long long getId() const { return _id; }

  /**
   * @return content of data entry
   */
  const std::string &getData() const { return _data; }
};

class StorageModuleBase {
protected:
  bool _is_initialized = false;
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;

public:
  StorageModuleBase(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) {
    this->_configuration = config;
    this->_logger = logger;
  }

  bool isInitialized() { return _is_initialized; }

  virtual ~StorageModuleBase() {
    _logger.reset();
    _configuration.reset();
  }

  /**
   * Initialize the storage module.
   */
  virtual void init() = 0;

  /**
   * Returns the count of stored requests.
   * @return count of stored requests
   */
  virtual long long RQCount() = 0;

  /**
   * Delete all stored requests.
   */
  virtual void RQClearAll() = 0;

  /**
   * Remove front request from the request queue.
   */
  virtual void RQRemoveFront() = 0;

  /**
   * Retrieve the element at the front of the queue. It does not deletes the element in the queue.
   * @return front request of the queue.
   */
  const virtual std::shared_ptr<DataEntry> RQPeekFront() = 0;

  /**
   * Retrieve all requests in the request queue without removing them.
   * @return a vector of requests.
   */
  virtual std::vector<std::shared_ptr<DataEntry>> RQPeekAll() = 0;

  /**
   * Remove the front request from the request queue if provided request's id and front request's id do match.
   * @param request: a shared pointer to front request
   */
  virtual void RQRemoveFront(std::shared_ptr<DataEntry> request) = 0;

  /**
   * Insert element into the request queue at the end.
   * @param request: content of the request
   */
  virtual void RQInsertAtEnd(const std::string &request) = 0;

  /**
   * Replace the content of a stored request, keeping its id and its place in the queue.
   * @param id: id of the request
   * @param request: new content of the request
   */
  virtual void RQUpdate(long long id, const std::string &request) {
    (void)id;
    (void)request;
  }

  /**
   * Remove the stored request with the id, wherever it is in the queue.
   * @param id: id of the request
   */
  virtual void RQRemove(long long id) { (void)id; }

  virtual void storeSDKBehaviorSettings(const std::string &sdk_behavior_settings) = 0;

  virtual std::string getSDKBehaviorSettings() = 0;

  /**
   * Store the health check counters.
   * @param state: the counters as JSON text, empty to clear them
   */
  virtual void storeHealthCheckState(const std::string &state) { (void)state; }

  /**
   * Read the stored health check counters.
   * @return the counters as JSON text, empty when none are stored
   */
  virtual std::string getHealthCheckState() { return std::string(); }

  /**
   * Store the device ID in use and its type, for a later start that is given no device ID.
   * @param state: the device ID and its type as JSON text
   */
  virtual void storeDeviceIdState(const std::string &state) { (void)state; }

  /**
   * Read the stored device ID and its type.
   * @return the device ID and its type as JSON text, empty when none are stored
   */
  virtual std::string getDeviceIdState() { return std::string(); }
};

} // namespace cly

#endif