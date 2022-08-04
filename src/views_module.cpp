#include "countly/views_module.hpp"

#include <chrono>

#define CLY_VIEW_KEY "[CLY]_view"

namespace cly {
class ViewsModule::ViewModuleImpl {
public:
  struct ViewInfo {
    std::string name;
    std::chrono::system_clock::time_point startTime;
  };

  ViewModuleImpl(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) : _cly{cly}, _logger{logger} {}
  std::string findViewIdByName(const std::string &name) {
    for (auto const &x : _viewsStartTime) {
      if (x.second.name == name) {
        return x.first;
      }
    }

    return {};
  }

  ~ViewModuleImpl() { _logger.reset(); }

  void _recordView(const std::string &eventID, std::map<std::string, std::string> segmentation = {}, bool isOpenView = false) {
    ViewInfo &v = _viewsStartTime[eventID];
    std::chrono::system_clock::duration duration = std::chrono::system_clock::now() - v.startTime;
    std::map<std::string, std::string> viewSegments;

    viewSegments["_idv"] = eventID;
    viewSegments["name"] = v.name;

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
      _viewsStartTime.erase(eventID);
    }
  }

  bool _isFirstView = true;
  std::map<std::string, ViewInfo> _viewsStartTime;

  cly::CountlyDelegates *_cly;
  std::shared_ptr<cly::LoggerModule> _logger;
};

ViewsModule::ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new ViewModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG, cly::utils::format("[ViewsModule] Initialized"));
}

ViewsModule::~ViewsModule() { impl.reset(); }

std::string ViewsModule::openView(const std::string &name, std::map<std::string, std::string> segmentation) {

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format("[ViewsModule] openView:  name = %s, segmentation = %s", name.c_str(), utils::mapToString(segmentation).c_str()));

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] openView: view name can not be null or empty!");
    return {};
  }

  std::string &viewId = cly::utils::generateEventID();
  ViewModuleImpl::ViewInfo v;
  v.name = name;
  v.startTime = std::chrono::system_clock::now();

  impl->_viewsStartTime[viewId] = v;

  std::map<std::string, std::string> viewSegments;

  impl->_recordView(viewId, segmentation, true);
  return viewId;
}

void ViewsModule::closeViewWithName(const std::string &name) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithName:  name = %s", name.c_str()));

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithName: view name can not be null or empty!");
    return;
  }

  std::string &viewId = impl->findViewIdByName(name);
  if (viewId.empty()) {
    cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithName:  Couldn't found "
                                            "view with name = %s",
                                            name.c_str());
    return;
  }

  impl->_recordView(viewId);
}

void ViewsModule::closeViewWithID(const std::string &viewId) {
  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithID:  viewId = %s", viewId.c_str()));

  if (viewId.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithID: viewId can not be null or empty!");
    return;
  }

  if (impl->_viewsStartTime.find(viewId) == impl->_viewsStartTime.end()) {
    cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithID:  Couldn't found "
                                            "view with viewId = %s",
                                            viewId.c_str());
    return;
  }

  impl->_recordView(viewId);
}
} // namespace cly