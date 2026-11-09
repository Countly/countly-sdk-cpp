#include "countly/consent_module.hpp"

#include <algorithm>
#include <iterator>

namespace cly {
namespace {
/**
 * The features this SDK offers for consent.
 */
const char *const FEATURES[] = {consent::SESSIONS, consent::EVENTS, consent::VIEWS, consent::LOCATION, consent::CRASHES, consent::USERS, consent::REMOTE_CONFIG, consent::METRICS, consent::FEEDBACK, consent::STAR_RATING};

/**
 * The consent flag of a feature, false when it was never set.
 */
bool flagOf(const std::map<std::string, bool> &given, const std::string &feature) {
  std::map<std::string, bool>::const_iterator found = given.find(feature);
  return found != given.end() && found->second;
}
} // namespace

namespace consent {
std::vector<std::string> allFeatures() { return std::vector<std::string>(std::begin(FEATURES), std::end(FEATURES)); }
} // namespace consent

bool ConsentModule::isFeature(const std::string &name) {
  for (const char *feature : FEATURES) {
    if (name == feature) {
      return true;
    }
  }
  return false;
}

void ConsentModule::setRequiredByApp(bool required) {
  std::lock_guard<std::mutex> lock(_mutex);
  _requiredByApp = required;
}

void ConsentModule::setRequiredByServer(int required) {
  std::lock_guard<std::mutex> lock(_mutex);
  _requiredByServer = required;
}

bool ConsentModule::requirementChangesWith(int requiredByServer) const {
  std::lock_guard<std::mutex> lock(_mutex);
  const bool requiredThen = requiredByServer < 0 ? _requiredByApp : requiredByServer == 1;
  return isRequiredLocked() != requiredThen;
}

bool ConsentModule::isRequired() const {
  std::lock_guard<std::mutex> lock(_mutex);
  return isRequiredLocked();
}

bool ConsentModule::getConsent(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_mutex);
  return getConsentLocked(name);
}

bool ConsentModule::isKnownName(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_mutex);
  return isFeature(name) || _groups.find(name) != _groups.end();
}

bool ConsentModule::isGroup(const std::string &name) const {
  std::lock_guard<std::mutex> lock(_mutex);
  return _groups.find(name) != _groups.end();
}

std::vector<std::string> ConsentModule::expand(const std::vector<std::string> &names, std::vector<std::string> &unknown) const {
  std::lock_guard<std::mutex> lock(_mutex);
  std::vector<std::string> features;
  const auto add = [&features](const std::string &feature) {
    if (std::find(features.begin(), features.end(), feature) == features.end()) {
      features.push_back(feature);
    }
  };

  for (const std::string &name : names) {
    if (isFeature(name)) {
      add(name);
      continue;
    }
    std::map<std::string, std::vector<std::string>>::const_iterator group = _groups.find(name);
    if (group == _groups.end()) {
      unknown.push_back(name);
      continue;
    }
    for (const std::string &feature : group->second) {
      add(feature);
    }
  }
  return features;
}

std::vector<std::string> ConsentModule::featuresToChange(const std::vector<std::string> &features, bool given) const {
  std::lock_guard<std::mutex> lock(_mutex);
  std::vector<std::string> changing;
  for (const std::string &feature : features) {
    if (flagOf(_given, feature) != given) {
      changing.push_back(feature);
    }
  }
  return changing;
}

void ConsentModule::setGiven(const std::vector<std::string> &features, bool given) {
  std::lock_guard<std::mutex> lock(_mutex);
  for (const std::string &feature : features) {
    _given[feature] = given;
  }
}

void ConsentModule::setOnlyGiven(const std::vector<std::string> &features) {
  std::lock_guard<std::mutex> lock(_mutex);
  _given.clear();
  for (const std::string &feature : features) {
    _given[feature] = true;
  }
}

std::vector<std::string> ConsentModule::featuresWithFlag(bool given) const {
  std::lock_guard<std::mutex> lock(_mutex);
  std::vector<std::string> features;
  for (const char *feature : FEATURES) {
    if (flagOf(_given, feature) == given) {
      features.push_back(feature);
    }
  }
  return features;
}

nlohmann::json ConsentModule::state() const {
  std::lock_guard<std::mutex> lock(_mutex);
  nlohmann::json state = nlohmann::json::object();
  for (const char *feature : FEATURES) {
    state[feature] = flagOf(_given, feature);
  }
  return state;
}

void ConsentModule::createGroup(const std::string &name, const std::vector<std::string> &features) {
  std::lock_guard<std::mutex> lock(_mutex);
  _groups[name] = features;
}

bool ConsentModule::isRequiredLocked() const { return _requiredByServer < 0 ? _requiredByApp : _requiredByServer == 1; }

bool ConsentModule::getConsentLocked(const std::string &name) const {
  if (!isRequiredLocked()) {
    return true;
  }
  std::map<std::string, std::vector<std::string>>::const_iterator group = _groups.find(name);
  if (group == _groups.end()) {
    return flagOf(_given, name);
  }
  for (const std::string &feature : group->second) {
    if (!flagOf(_given, feature)) {
      return false;
    }
  }
  return true;
}
} // namespace cly
