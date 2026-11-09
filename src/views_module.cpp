#include "countly/views_module.hpp"

#include "countly/consent_module.hpp"
#include "countly/internal_limits.hpp"

#include <chrono>
#include <mutex>
#include <vector>

#define CLY_VIEW_KEY "[CLY]_view"

namespace cly {
class ViewsModule::ViewModuleImpl {
  struct ViewInfo {
  public:
    std::string name;
    std::string viewId;
    // steady_clock, so a system clock change can't distort the view duration
    std::chrono::steady_clock::time_point startTime;
  };

private:
  // Guards _isFirstView and _viewsStartTime. Views are opened and closed from
  // whichever thread the integrator uses, and nothing else in the SDK
  // serialises those calls, so the module has to do it itself.
  std::mutex _views_mutex;
  bool _isFirstView = true;
  std::map<std::string, std::shared_ptr<ViewInfo>> _viewsStartTime;

  cly::CountlyDelegates *_cly;

  // Call with _views_mutex held.
  std::shared_ptr<ViewInfo> findViewByName(const std::string &name) {
    for (auto &x : _viewsStartTime) {
      if (x.second->name == name) {
        return x.second;
      }
    }

    return nullptr;
  }

  /**
   * Call with _views_mutex NOT held. This calls back into Countly::addEvent,
   * which takes the instance mutex and can invoke the integrator's log callback,
   * and that callback is free to call back into this module. Holding the view
   * lock across it would risk a deadlock, so the shared state is read and
   * updated before this runs and the outcome is passed in.
   *
   * @param isFirstView: only meaningful when isOpenView is true
   */
  void _recordView(std::shared_ptr<ViewInfo> v, const std::map<std::string, std::string> &segmentation, bool isOpenView, bool isFirstView = false) {
    double duration = 0;
    std::map<std::string, std::string> viewSegments;

    if (isOpenView) {
      viewSegments["visit"] = "1";

      if (isFirstView) {
        viewSegments["start"] = "1";
      }

      for (auto key_value : segmentation) {
        auto itr = viewSegments.find(key_value.first);
        if (itr != viewSegments.end()) {
          (*itr).second = key_value.second;
        } else {
          viewSegments[key_value.first] = key_value.second;
        }
      }
    } else {

      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - v->startTime);
      duration = dur.count();
    }

    viewSegments["_idv"] = v->viewId;
    viewSegments["name"] = v->name;

    _cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration);
  }

public:
  std::shared_ptr<cly::LoggerModule> _logger;
  std::weak_ptr<ConfigurationProvider> _configProvider;
  std::shared_ptr<ConsentModule> _consent;
  ViewModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly(cly), _logger(logger) {}

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
   * Takes every running view out of the map and records its end event, or drops them while view tracking is off.
   */
  void _stopAllViews() {
    std::vector<std::shared_ptr<ViewInfo>> running;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      for (const auto &entry : _viewsStartTime) {
        running.push_back(entry.second);
      }
      _viewsStartTime.clear();
    }

    std::shared_ptr<ConfigurationProvider> config = _configProvider.lock();
    if (!config || config->isViewTrackingEnabled() == false) {
      _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _stopAllViews, View tracking is disabled, the running views are dropped.");
      return;
    }
    for (const std::shared_ptr<ViewInfo> &view : running) {
      _recordView(view, {}, false);
    }
  }

  std::string _openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {
    SDKLimits lim{COUNTLY_MAX_KEY_LENGTH_DEFAULT, COUNTLY_MAX_VALUE_SIZE_DEFAULT, COUNTLY_MAX_SEGMENTATION_VALUES_DEFAULT, COUNTLY_MAX_BREADCRUMB_COUNT_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINES_PER_THREAD_DEFAULT, COUNTLY_MAX_STACK_TRACE_LINE_LENGTH_DEFAULT};
    if (std::shared_ptr<ConfigurationProvider> config = _configProvider.lock()) {
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
    std::string limitedName = cly::limits::truncateString(name, lim.maxKeyLength);
    std::map<std::string, std::string> limitedSeg = cly::limits::applySegmentationLimits(segmentation, lim);

    ViewModuleImpl::ViewInfo *v = new ViewModuleImpl::ViewInfo();
    v->name = limitedName;
    v->viewId = cly::utils::generateEventID();
    v->startTime = std::chrono::steady_clock::now();

    std::shared_ptr<ViewModuleImpl::ViewInfo> ptr(v);

    bool isFirstView = false;
    {
      std::lock_guard<std::mutex> lk(_views_mutex);
      _viewsStartTime[ptr->viewId] = ptr;
      // Claimed under the lock, so 'start' goes on exactly one view even when
      // two threads open their first view at the same time.
      isFirstView = _isFirstView;
      _isFirstView = false;
    }

    _recordView(ptr, limitedSeg, true, isFirstView);
    return ptr->viewId;
  }

  void _closeViewWithName(const std::string &name) {
    if (std::shared_ptr<ConfigurationProvider> config = _configProvider.lock()) {
      if (config->isViewTrackingEnabled() == false) {
        _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _closeViewWithName, View tracking is disabled. Not closing view.");
        return;
      }
    } else {
      _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] _closeViewWithName, ConfigurationProvider unavailable.");
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
    _recordView(v, {}, false);
  }

  void _closeViewWithID(const std::string &viewId) {
    if (std::shared_ptr<ConfigurationProvider> config = _configProvider.lock()) {
      if (config->isViewTrackingEnabled() == false) {
        _logger->log(LogLevel::DEBUG, "[Countly] [ViewsModule] _closeViewWithID, View tracking is disabled. Not closing view.");
        return;
      }
    } else {
      _logger->log(LogLevel::WARNING, "[Countly] [ViewsModule] _closeViewWithID, ConfigurationProvider unavailable.");
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

    _recordView(v, {}, false);
  }
};

ViewsModule::ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new ViewModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[Countly] [ViewsModule] Initialized"));
}

ViewsModule::~ViewsModule() { impl.reset(); }

std::string ViewsModule::openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] openView, name = [%s], segmentation = [%s]", name.c_str(), utils::mapToString(segmentation).c_str()));

  if (!impl->hasConsent("openView")) {
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

void ViewsModule::closeViewWithName(const std::string &name) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] closeViewWithName, name = [%s]", name.c_str()));

  if (!impl->hasConsent("closeViewWithName")) {
    return;
  }
  impl->_logger->recordUsage("views", "stop");
  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[Countly] [ViewsModule] closeViewWithName, view name can not be null or empty!");
    impl->_logger->recordLogCode("w700");
    return;
  }
  impl->_closeViewWithName(name);
}

void ViewsModule::closeViewWithID(const std::string &viewId) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[Countly] [ViewsModule] closeViewWithID, viewId = [%s]", viewId.c_str()));

  if (!impl->hasConsent("closeViewWithID")) {
    return;
  }
  impl->_logger->recordUsage("views", "stop");
  if (viewId.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[Countly] [ViewsModule] closeViewWithID, viewId can not be null or empty!");
    impl->_logger->recordLogCode("w700");
    return;
  }

  impl->_closeViewWithID(viewId);
}

void ViewsModule::setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider) { impl->_configProvider = std::move(provider); }

void ViewsModule::setConsentModule(std::shared_ptr<ConsentModule> consent) { impl->_consent = std::move(consent); }

void ViewsModule::stopAllViews() { impl->_stopAllViews(); }

} // namespace cly