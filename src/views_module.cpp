#include "countly/views_module.hpp"

#include "countly/consent_module.hpp"
#include "countly/event.hpp"
#include "countly/internal_limits.hpp"

#include <chrono>
#include <mutex>
#include <set>
#include <vector>

#define CLY_VIEW_KEY "[CLY]_view"

namespace cly {
namespace {
/**
 * Segmentation keys of view events that custom segmentation can't replace.
 */
const std::set<std::string> RESERVED_VIEW_KEYS = {"name", "visit", "start", "segment", "bounce", "exit", "view", "domain", "dur", "_idv"};
} // namespace

class ViewsModule::ViewModuleImpl {
  struct ViewInfo {
  public:
    std::string name;
    std::string viewId;
    // The view that was current when this one started, sent as "pvid" with this view's events
    std::string previousViewId;
    // Start order, so that the last started view is the one closed by name
    unsigned long long sequence = 0;
    // steady_clock, so a system clock change can't distort the view duration
    std::chrono::steady_clock::time_point startTime;
  };

private:
  // Guards every member below. Views are opened and closed from whichever
  // thread the integrator uses, and nothing else in the SDK serialises those
  // calls, so the module has to do it itself.
  std::mutex _views_mutex;
  bool _isFirstView = true;
  std::string _currentViewId;
  unsigned long long _sequence = 0;
  std::map<std::string, std::shared_ptr<ViewInfo>> _viewsStartTime;
  std::weak_ptr<ConfigurationProvider> _configProvider;

  cly::CountlyDelegates *_cly;

  // Call with _views_mutex held.
  std::shared_ptr<ViewInfo> findViewByName(const std::string &name) {
    std::shared_ptr<ViewInfo> found;
    for (auto &x : _viewsStartTime) {
      if (x.second->name == name && (found == nullptr || x.second->sequence > found->sequence)) {
        found = x.second;
      }
    }

    return found;
  }

  /**
   * Call with _views_mutex NOT held. This calls back into Countly::addEvent,
   * which takes the instance mutex and can invoke the integrator's log callback,
   * and that callback is free to call back into this module. Holding the view
   * lock across it would risk a deadlock, so the shared state is read and
   * updated before this runs and the outcome is passed in.
   *
   * @param segmentation: custom segmentation, already limited and without reserved keys
   * @param isFirstView: only meaningful when isOpenView is true
   */
  void _recordView(std::shared_ptr<ViewInfo> v, const std::map<std::string, std::string> &segmentation, bool isOpenView, bool isFirstView = false) {
    double duration = 0;
    std::map<std::string, std::string> viewSegments = segmentation;

    if (isOpenView) {
      viewSegments["visit"] = "1";

      if (isFirstView) {
        viewSegments["start"] = "1";
      }
    } else {

      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - v->startTime);
      duration = dur.count();
    }

    viewSegments["_idv"] = v->viewId;
    viewSegments["name"] = v->name;
    const std::string platform = _cly->PlatformName();
    if (!platform.empty()) {
      viewSegments["segment"] = platform;
    }

    cly::Event event(CLY_VIEW_KEY, 1, 0, duration);
    for (const auto &key_value : viewSegments) {
      event.addSegmentation(key_value.first, key_value.second);
    }
    ViewsModule::setEventField(event, "id", v->viewId);
    ViewsModule::setEventField(event, "pvid", v->previousViewId);
    _cly->addEvent(event);
  }

public:
  std::shared_ptr<cly::LoggerModule> _logger;
  std::shared_ptr<ConsentModule> _consent;
  ViewModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly(cly), _logger(logger) {}

  /**
   * The SDK behavior settings in use, empty before start() wires the module up.
   */
  std::shared_ptr<ConfigurationProvider> provider() {
    std::lock_guard<std::mutex> lk(_views_mutex);
    return _configProvider.lock();
  }

  /**
   * Sets the SDK behavior settings in use.
   */
  void setProvider(std::weak_ptr<ConfigurationProvider> configProvider) {
    std::lock_guard<std::mutex> lk(_views_mutex);
    _configProvider = std::move(configProvider);
  }

  /**
   * Whether start() has wired the module up, logged as a warning and counted as w101 when it has not.
   */
  bool isInitialized(const char *caller) {
    if (provider()) {
      return true;
    }
    _logger->log(LogLevel::WARNING, std::string("[Countly] [ViewsModule] ") + caller + ", SDK is not initialized.");
    _logger->recordLogCode("w101");
    return false;
  }

  ~ViewModuleImpl() { _logger.reset(); }

  /**
   * Whether the "views" consent allows the call, logged at debug level when it does not.
   */
  bool hasConsent(const char *caller) {
    if (!_consent || _consent->getConsent(consent::VIEWS)) {
      return true;
    }
    _logger->log(LogLevel::DEBUG, std::string("[Countly] [ViewsModule] ") + caller + ", the 'views' consent is not given, the call is ignored.");
    return false;
  }

  /**
   * Custom segmentation without the reserved view keys, which are logged as a warning and ignored, and with the
   * internal limits applied.
   */
  std::map<std::string, std::string> customSegmentation(const std::map<std::string, std::string> &segmentation, const SDKLimits &lim, const std::string &caller) {
    std::map<std::string, std::string> custom;
    for (const auto &key_value : segmentation) {
      if (RESERVED_VIEW_KEYS.count(key_value.first)) {
        _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] " + caller + ", [" + key_value.first + "] is a reserved view segmentation key and is ignored.");
        _logger->recordLogCode("w700");
        continue;
      }
      custom[key_value.first] = key_value.second;
    }
    return cly::limits::limitSegmentation(custom, lim, _logger.get(), "[ViewsModule] " + caller);
  }

  /**
   * Takes every running view out of the map and records its end event, or drops them while view tracking is off.
   */
  void _stopAllViews(const std::map<std::string, std::string> &segmentation, const std::string &caller) {
    std::vector<std::shared_ptr<ViewInfo>> running;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      for (const auto &entry : _viewsStartTime) {
        running.push_back(entry.second);
      }
      _viewsStartTime.clear();
    }

    std::shared_ptr<ConfigurationProvider> config = provider();
    if (!config || config->isViewTrackingEnabled() == false) {
      _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _stopAllViews, View tracking is disabled, the running views are dropped.");
      return;
    }
    const std::map<std::string, std::string> custom = customSegmentation(segmentation, config->getLimits(), caller);
    for (const std::shared_ptr<ViewInfo> &view : running) {
      _recordView(view, custom, false);
    }
  }

  std::string _openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {
    SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
    if (std::shared_ptr<ConfigurationProvider> config = provider()) {
      if (config->isViewTrackingEnabled() == false) {
        _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _openView, View tracking is disabled. Not opening view.");
        return "";
      }
      lim = config->getLimits();
    } else {
      _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] _openView, ConfigurationProvider unavailable.");
      return "";
    }

    // A view name is a "key" per the guide -> maxKeyLength. Developer segmentation
    // is limited before internal keys (visit/start/_idv/name) are merged.
    const std::string limitedName = cly::limits::limitKey(name, lim.maxKeyLength, _logger.get(), "[ViewsModule] openView");
    const std::map<std::string, std::string> limitedSeg = customSegmentation(segmentation, lim, "openView");
    // Asked before taking the view lock: the answer takes the instance mutex
    const bool sessionRunning = _cly->IsSessionRunning();

    std::shared_ptr<ViewModuleImpl::ViewInfo> ptr = std::make_shared<ViewModuleImpl::ViewInfo>();
    ptr->name = limitedName;
    ptr->viewId = cly::utils::generateEventID();
    ptr->startTime = std::chrono::steady_clock::now();

    bool isFirstView = false;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      ptr->previousViewId = _currentViewId;
      ptr->sequence = ++_sequence;
      _currentViewId = ptr->viewId;
      _viewsStartTime[ptr->viewId] = ptr;
      // Claimed under the lock, so 'start' goes on exactly one view even when
      // two threads open their first view at the same time.
      isFirstView = _isFirstView && sessionRunning;
      if (isFirstView) {
        _isFirstView = false;
      }
    }

    _recordView(ptr, limitedSeg, true, isFirstView);
    return ptr->viewId;
  }

  void _closeViewWithName(const std::string &name, const std::map<std::string, std::string> &segmentation) {
    std::shared_ptr<ConfigurationProvider> config = provider();
    if (!config) {
      _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] _closeViewWithName, ConfigurationProvider unavailable.");
      return;
    }
    if (config->isViewTrackingEnabled() == false) {
      _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _closeViewWithName, View tracking is disabled. Not closing view.");
      return;
    }
    // The view is taken out of the map under the lock, so two threads closing
    // the same view cannot both record it.
    std::shared_ptr<ViewModuleImpl::ViewInfo> v;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      v = findViewByName(name);
      if (v != nullptr) {
        _viewsStartTime.erase(v->viewId);
      }
    }

    if (v == nullptr) {
      _logger->log(cly::LogLevel::WARNING, cly::utils::format_string("[Countly] [ViewsModule] _closeViewWithName, Couldn't find "
                                                                     "view with name = [%s]",
                                                                     name.c_str()));
      _logger->recordLogCode("w730");
      return;
    }
    _recordView(v, customSegmentation(segmentation, config->getLimits(), "closeViewWithName"), false);
  }

  void _closeViewWithID(const std::string &viewId, const std::map<std::string, std::string> &segmentation) {
    std::shared_ptr<ConfigurationProvider> config = provider();
    if (!config) {
      _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] _closeViewWithID, ConfigurationProvider unavailable.");
      return;
    }
    if (config->isViewTrackingEnabled() == false) {
      _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _closeViewWithID, View tracking is disabled. Not closing view.");
      return;
    }

    std::shared_ptr<ViewModuleImpl::ViewInfo> v;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      std::map<std::string, std::shared_ptr<ViewInfo>>::iterator it = _viewsStartTime.find(viewId);
      if (it != _viewsStartTime.end()) {
        v = it->second;
        _viewsStartTime.erase(it);
      }
    }

    if (v == nullptr) {
      _logger->log(cly::LogLevel::WARNING, cly::utils::format_string("[Countly] [ViewsModule] _closeViewWithID, Couldn't find "
                                                                     "view with viewId = [%s]",
                                                                     viewId.c_str()));
      _logger->recordLogCode("w730");
      return;
    }

    _recordView(v, customSegmentation(segmentation, config->getLimits(), "closeViewWithID"), false);
  }

  void resetFirstView() {
    std::lock_guard<std::mutex> lk(_views_mutex);
    _isFirstView = true;
  }

  void forgetViews() {
    std::lock_guard<std::mutex> lk(_views_mutex);
    _currentViewId.clear();
    _isFirstView = true;
  }

  std::string currentViewId() {
    std::lock_guard<std::mutex> lk(_views_mutex);
    return _currentViewId;
  }
};

ViewsModule::ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new ViewModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[Countly] [ViewsModule] Initialized"));
}

ViewsModule::~ViewsModule() { impl.reset(); }

std::string ViewsModule::openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] openView, name = [%s], segmentation = [%s]", name.c_str(), utils::mapToString(segmentation).c_str()));

  if (!impl->isInitialized("openView") || !impl->hasConsent("openView")) {
    return {};
  }
  impl->_logger->recordUsage("views", "start");
  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[Countly] [ViewsModule] openView, view name can not be null or empty!");
    impl->_logger->recordLogCode("w710");
    return {};
  }

  return impl->_openView(name, segmentation);
}

void ViewsModule::closeViewWithName(const std::string &name, const std::map<std::string, std::string> &segmentation) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] closeViewWithName, name = [%s]", name.c_str()));

  if (!impl->isInitialized("closeViewWithName") || !impl->hasConsent("closeViewWithName")) {
    return;
  }
  impl->_logger->recordUsage("views", "stop");
  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[Countly] [ViewsModule] closeViewWithName, view name can not be null or empty!");
    impl->_logger->recordLogCode("w700");
    return;
  }
  impl->_closeViewWithName(name, segmentation);
}

void ViewsModule::closeViewWithID(const std::string &viewId, const std::map<std::string, std::string> &segmentation) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] closeViewWithID, viewId = [%s]", viewId.c_str()));

  if (!impl->isInitialized("closeViewWithID") || !impl->hasConsent("closeViewWithID")) {
    return;
  }
  impl->_logger->recordUsage("views", "stop");
  if (viewId.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[Countly] [ViewsModule] closeViewWithID, viewId can not be null or empty!");
    impl->_logger->recordLogCode("w700");
    return;
  }

  impl->_closeViewWithID(viewId, segmentation);
}

void ViewsModule::stopAllViews(const std::map<std::string, std::string> &segmentation) {
  impl->_logger->log(cly::LogLevel::INFO, "[Countly] [ViewsModule] stopAllViews, segmentation = [" + utils::mapToString(segmentation) + "]");

  if (!impl->isInitialized("stopAllViews") || !impl->hasConsent("stopAllViews")) {
    return;
  }
  impl->_logger->recordUsage("views", "stopAll");
  impl->_stopAllViews(segmentation, "stopAllViews");
}

void ViewsModule::setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider) { impl->setProvider(std::move(provider)); }

void ViewsModule::setConsentModule(std::shared_ptr<ConsentModule> consent) { impl->_consent = std::move(consent); }

void ViewsModule::stopRunningViews() { impl->_stopAllViews({}, "stopRunningViews"); }

void ViewsModule::resetFirstView() { impl->resetFirstView(); }

void ViewsModule::forgetViews() { impl->forgetViews(); }

std::string ViewsModule::currentViewId() { return impl->currentViewId(); }

void ViewsModule::setEventField(cly::Event &event, const std::string &key, const std::string &value) { event.object[key] = value; }

} // namespace cly
