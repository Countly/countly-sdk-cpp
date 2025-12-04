#ifndef USER_PROFILE_MODULE_HPP_
#define USER_PROFILE_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "countly/request_module.hpp"

namespace cly {
class UserProfileModule {

public:
  ~UserProfileModule();
  UserProfileModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex);

  /**
   * Sets a property for the user profile. Overwrites existing value.
   * @param key: Property key
   * @param value: Property value
   */
  void setProperty(const std::string &key, const nlohmann::json &value);

  /**
   * Sets multiple properties for the user profile. Overwrites existing values.
   * @param values: Map of key-value pairs to set
   */
  void setProperties(const std::map<std::string, nlohmann::json> &values);

  /**
   * Sets a property for the user profile only if it does not already exist.
   * @param key: Property key
   * @param value: Property value
   */
  void setOnce(const std::string &key, const nlohmann::json &value);

  /**
   * Increments a numeric property by a given value. Defaults to incrementing by 1.
   * @param key: Property key
   * @param value: Amount to increment by
   */
  void incrementBy(const std::string &key, double value = 1);

  /**
   * Multiplies a numeric property by a given value.
   * @param key: Property key
   * @param value: Amount to multiply by
   */
  void multiply(const std::string &key, double value);

  /**
   * Saves the maximum of the current and given value for a numeric property.
   * @param key: Property key
   * @param value: Value to compare with
   */
  void saveMax(const std::string &key, double value);

  /**
   * Saves the minimum of the current and given value for a numeric property.
   * @param key: Property key
   * @param value: Value to compare with
   */
  void saveMin(const std::string &key, double value);

  /**
   * Pushes a value to an array property.
   * @param key: Property key
   * @param value: Value to push
   */
  void push(const std::string &key, const nlohmann::json &value);

  /**
   * Pushes a unique value to an array property.
   * @param key: Property key
   * @param value: Value to push uniquely
   */
  void pushUnique(const std::string &key, const nlohmann::json &value);

  /**
   * Pulls a value from an array property.
   * @param key: Property key
   * @param value: Value to pull
   */
  void pull(const std::string &key, const nlohmann::json &value);

  /**
   * Saves all changes to the user profile.
   */
  void save();

private:
  class UserProfileModuleImpl;
  std::unique_ptr<UserProfileModuleImpl> impl;
};
} // namespace cly
#endif
