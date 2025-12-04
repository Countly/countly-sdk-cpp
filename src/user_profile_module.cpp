#include "countly/user_profile_module.hpp"
#include "countly/request_module.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <iterator>

// define namespace
namespace cly {
// define UserProfileModuleImpl class
class UserProfileModule::UserProfileModuleImpl {
public:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestModule> _requestModule;
  std::shared_ptr<std::mutex> _mutex;
  
  std::map<std::string, nlohmann::json> _userProperties;
  UserProfileModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) : _configuration(config), _logger(logger), _requestModule(requestModule), _mutex(mutex) {}

  // destructor to reset logger
  ~UserProfileModuleImpl() { _logger.reset(); }
};

// destructor to reset implementation
UserProfileModule::~UserProfileModule() {
  impl->_userProperties.clear();
  impl.reset();
}
UserProfileModule::UserProfileModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestModule> requestModule, std::shared_ptr<std::mutex> mutex) {
  impl.reset(new UserProfileModuleImpl(config, logger, requestModule, mutex));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[UserProfileModule] Initialized"));
  impl->_userProperties = {};
}

// function to set a property
void UserProfileModule::setProperty(const std::string &key, const nlohmann::json &value) {
  impl->_mutex->lock();
  impl->_userProperties[key] = value;
  impl->_mutex->unlock();
}

// function to set multiple properties
void UserProfileModule::setProperties(const std::map<std::string, nlohmann::json> &values) {
  impl->_mutex->lock();
  for (const auto &pair : values) {
    impl->_userProperties[pair.first] = pair.second;
  }
  impl->_mutex->unlock();
}

// function to set a property once
void UserProfileModule::setOnce(const std::string &key, const nlohmann::json &value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) == impl->_userProperties.end()) {
    impl->_userProperties[key] = value;
  }
  impl->_mutex->unlock();
}

// function to increment a property
void UserProfileModule::incrementBy(const std::string &key, double value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_number()) {
    impl->_userProperties[key] = impl->_userProperties[key].get<double>() + value;
  } else {
    impl->_userProperties[key] = value;
  }
  impl->_mutex->unlock();
}

// function to multiply a property
void UserProfileModule::multiply(const std::string &key, double value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_number()) {
    impl->_userProperties[key] = impl->_userProperties[key].get<double>() * value;
  } else {
    impl->_userProperties[key] = 0;
  }
  impl->_mutex->unlock();
}

// function to save max of a property
void UserProfileModule::saveMax(const std::string &key, double value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_number()) {
    impl->_userProperties[key] = std::max(impl->_userProperties[key].get<double>(), value);
  } else {
    impl->_userProperties[key] = value;
  }
  impl->_mutex->unlock();
}

// function to save min of a property
void UserProfileModule::saveMin(const std::string &key, double value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_number()) {
    impl->_userProperties[key] = std::min(impl->_userProperties[key].get<double>(), value);
  } else {
    impl->_userProperties[key] = value;
  }
  impl->_mutex->unlock();
}

// function to push a value to an array property
void UserProfileModule::push(const std::string &key, const nlohmann::json &value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_array()) {
    impl->_userProperties[key].push_back(value);
  } else {
    impl->_userProperties[key] = nlohmann::json::array({value});
  }
  impl->_mutex->unlock();
}

// function to push a unique value to an array property
void UserProfileModule::pushUnique(const std::string &key, const nlohmann::json &value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_array()) {
    if (std::find(impl->_userProperties[key].begin(), impl->_userProperties[key].end(), value) == impl->_userProperties[key].end()) {
      impl->_userProperties[key].push_back(value);
    }
  } else {
    impl->_userProperties[key] = nlohmann::json::array({value});
  }
  impl->_mutex->unlock();
}

// function to pull a value from an array property
void UserProfileModule::pull(const std::string &key, const nlohmann::json &value) {
  impl->_mutex->lock();
  if (impl->_userProperties.find(key) != impl->_userProperties.end() && impl->_userProperties[key].is_array()) {
    auto &array = impl->_userProperties[key];
    array.erase(std::remove(array.begin(), array.end(), value), array.end());
  }
  impl->_mutex->unlock();
}

// function to save all changes to the user profile
void UserProfileModule::save() {
  impl->_mutex->lock();
  if (!impl->_userProperties.empty()) {
    impl->_requestModule->addRequestToQueue({{"user_details", nlohmann::json({{"$set", impl->_userProperties}}).dump()}});
    impl->_userProperties.clear();
  }
  impl->_mutex->unlock();
}

} // namespace cly