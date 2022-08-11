#include "countly/views_module.hpp"

#include <chrono>

#define CLY_VIEW_KEY "[CLY]_view"

namespace cly {
class ViewsModule::ViewModuleImpl {
  struct ViewInfo {
  public:
    std::string name;
    std::string viewId;
    std::chrono::system_clock::time_point startTime;
  };

private:
  bool _isFirstView = true;
  std::map<std::string, std::shared_ptr<ViewInfo>> _viewsStartTime;

  cly::CountlyDelegates *_cly;

  std::shared_ptr<ViewInfo> findViewByName(const std::string &name) {
    for (auto &x : _viewsStartTime) {
      if (x.second->name == name) {
        return x.second;
      }
    }

    return nullptr;
  }
  void _recordView(std::shared_ptr<ViewInfo> v, const std::map<std::string, std::string> &segmentation, bool isOpenView) {
    std::chrono::system_clock::duration duration = std::chrono::system_clock::now() - v->startTime;
    std::map<std::string, std::string> viewSegments;

    viewSegments["_idv"] = v->viewId;
    viewSegments["name"] = v->name;

    if (isOpenView) {
      viewSegments["visit"] = "1";
      viewSegments["start"] = _isFirstView ? "1" : "0";

      for (auto key_value : segmentation) {
        auto itr = viewSegments.find(key_value.first);
        if (itr != viewSegments.end()) {
          (*itr).second = key_value.second;
        } else {
          viewSegments[key_value.first] = key_value.second;
        }
      }
    }

    _cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration.count());
    if (isOpenView) {
      _isFirstView = false;
    } else {
      _viewsStartTime.erase(v->viewId);
    }
  }

public:
  std::shared_ptr<cly::LoggerModule> _logger;
  ViewModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly(cly), _logger(logger) {}

  ~ViewModuleImpl() { _logger.reset(); }

  std::string _openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {
    ViewModuleImpl::ViewInfo *v = new ViewModuleImpl::ViewInfo();
    v->name = name;
    v->viewId = cly::utils::generateEventID();
    v->startTime = std::chrono::system_clock::now();

    std::shared_ptr<ViewModuleImpl::ViewInfo> ptr(v);

    _viewsStartTime[ptr->viewId] = ptr;

    _recordView(ptr, segmentation, true);
    return ptr->viewId;
  }

  void _closeViewWithName(const std::string &name) {
    std::shared_ptr<ViewModuleImpl::ViewInfo> v = findViewByName(name);
    if (v == nullptr) {
      cly::LogLevel::WARNING, cly::utils::format_string("[ViewModuleImpl] _closeViewWithName:  Couldn't found "
                                                        "view with name = %s",
                                                        name.c_str());
      return;
    }
    _recordView(v, {}, false);
  }

  void _closeViewWithID(const std::string &viewId) {

    if (_viewsStartTime.find(viewId) == _viewsStartTime.end()) {
      cly::LogLevel::WARNING, cly::utils::format_string("[ViewModuleImpl] _closeViewWithID:  Couldn't found "
                                                        "view with viewId = %s",
                                                        viewId.c_str());
      return;
    }

    _recordView(_viewsStartTime[viewId], {}, false);
  }
};

ViewsModule::ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new ViewModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format_string("[ViewsModule] Initialized"));
}

ViewsModule::~ViewsModule() { impl.reset(); }

std::string ViewsModule::openView(const std::string &name, const std::map<std::string, std::string> &segmentation) {

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[ViewsModule] openView:  name = %s, segmentation = %s", name.c_str(), utils::mapToString(segmentation).c_str()));

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] openView: view name can not be null or empty!");
    return {};
  }

  return impl->_openView(name, segmentation);
}

void ViewsModule::closeViewWithName(const std::string &name) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[ViewsModule] closeViewWithName:  name = %s", name.c_str()));

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithName: view name can not be null or empty!");
    return;
  }
  impl->_closeViewWithName(name);
}

void ViewsModule::closeViewWithID(const std::string &viewId) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format_string("[ViewsModule] closeViewWithID:  viewId = %s", viewId.c_str()));

  if (viewId.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithID: viewId can not be null or empty!");
    return;
  }

  impl->_closeViewWithID(viewId);
}
} // namespace cly