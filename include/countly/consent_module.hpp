#ifndef CONSENT_MODULE_HPP_
#define CONSENT_MODULE_HPP_

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace cly {
/**
 * The names of the features an app gives or removes consent for, as the server receives them.
 */
namespace consent {
constexpr const char *SESSIONS = "sessions";
constexpr const char *EVENTS = "events";
constexpr const char *VIEWS = "views";
constexpr const char *LOCATION = "location";
constexpr const char *CRASHES = "crashes";
constexpr const char *USERS = "users";
constexpr const char *REMOTE_CONFIG = "remote-config";
constexpr const char *METRICS = "metrics";
constexpr const char *FEEDBACK = "feedback";
constexpr const char *STAR_RATING = "star-rating";

/**
 * The names of every feature this SDK offers for consent, for example to build a consent screen
 * or to give every consent at start with setConsentEnabled(cly::consent::allFeatures()).
 */
std::vector<std::string> allFeatures();
} // namespace consent

/**
 * The consent state of one SDK instance: whether consent is required, which features have consent
 * and the feature groups the app created. Kept in memory only. Thread safe; it never calls out of
 * itself while holding its lock.
 */
class ConsentModule {
public:
  /**
   * Whether the name is a feature this SDK offers for consent.
   */
  static bool isFeature(const std::string &name);

  /**
   * Sets whether the app requires consent.
   */
  void setRequiredByApp(bool required);

  /**
   * Sets the server's "cr" setting: -1 while the SDK behavior settings don't contain it, otherwise 0 or 1.
   */
  void setRequiredByServer(int required);

  /**
   * Whether the requirement would change if the server's "cr" setting became the given value.
   */
  bool requirementChangesWith(int requiredByServer) const;

  /**
   * Whether consent is required: the server's "cr" while the settings contain it, otherwise the app's choice.
   */
  bool isRequired() const;

  /**
   * Whether the feature, or every feature of the group, has consent. Always true while consent is
   * not required; false for a name that is neither a feature nor a group.
   */
  bool getConsent(const std::string &name) const;

  /**
   * Whether the name is a feature this SDK offers or a group the app created.
   */
  bool isKnownName(const std::string &name) const;

  /**
   * Whether the name is a group the app created.
   */
  bool isGroup(const std::string &name) const;

  /**
   * Expands feature and group names into feature names, without duplicates and in the order given.
   * Names that are neither are added to 'unknown'.
   */
  std::vector<std::string> expand(const std::vector<std::string> &names, std::vector<std::string> &unknown) const;

  /**
   * The given features whose own consent flag differs from 'given', regardless of the requirement.
   */
  std::vector<std::string> featuresToChange(const std::vector<std::string> &features, bool given) const;

  /**
   * Sets the consent flag of the given features.
   */
  void setGiven(const std::vector<std::string> &features, bool given);

  /**
   * Gives consent to exactly the given features and removes it from every other one.
   */
  void setOnlyGiven(const std::vector<std::string> &features);

  /**
   * The features whose own consent flag is set, or not set when 'given' is false.
   */
  std::vector<std::string> featuresWithFlag(bool given) const;

  /**
   * The consent flag of every feature this SDK offers, as the consent request sends it.
   */
  nlohmann::json state() const;

  /**
   * Stores a group of features under the name, replacing a group of that name.
   */
  void createGroup(const std::string &name, const std::vector<std::string> &features);

private:
  mutable std::mutex _mutex;
  bool _requiredByApp = false;
  int _requiredByServer = -1;
  std::map<std::string, bool> _given;
  std::map<std::string, std::vector<std::string>> _groups;

  bool isRequiredLocked() const;
  bool getConsentLocked(const std::string &name) const;
};
} // namespace cly
#endif
