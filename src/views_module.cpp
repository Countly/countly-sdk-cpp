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
  std::string findViewUUIDByName(const std::string &name) {
    for (auto const &x : _viewsStartTime) {
      if (x.second.name == name) {
        return x.first;
      }
    }

    return {};
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

ViewsModule::~ViewsModule() { impl->_logger.reset(); }

std::string ViewsModule::openView(const std::string &name, std::map<std::string, std::string> segmentation) {

  impl->_logger->log(cly::LogLevel::INFO, cly::utils::format("[ViewsModule] openView:  name = %s, segmentation = %s", name.c_str(), utils::mapToString(segmentation).c_str()));

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] openView: view name can not be null or empty!");
    return {};
  }

  std::string &uuid = cly::utils::generateEventID();
  ViewModuleImpl::ViewInfo v;
  v.name = name;
  v.startTime = std::chrono::system_clock::now();

  impl->_viewsStartTime[uuid] = v;

  std::map<std::string, std::string> viewSegments;

  viewSegments["name"] = name;
  //viewSegments["segment"] = "cpp";
  viewSegments["visit"] = "1";
  viewSegments["_idv"] = uuid;
  viewSegments["start"] = impl->_isFirstView ? "1" : "0";

  for (auto key_value : segmentation) {
    auto itr = viewSegments.find(key_value.first);
    if (itr != viewSegments.end()) {
      (*itr).second = key_value.second;
    } else {
      viewSegments[key_value.first] = key_value.second;
    }
  }

  impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1);

  impl->_isFirstView = false;
  return uuid;
}

void ViewsModule::closeViewWithName(const std::string &name) {
  cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithName:  name = %s", name.c_str());

  if (name.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithName: view name can not be null or empty!");
    return;
  }

  std::string &uuid = impl->findViewUUIDByName(name);
  if (uuid.empty()) {
    cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithName:  Couldn't found "
                                            "view with name = %s",
                                            name.c_str());
    return;
  }

  _closeView(uuid);
}

void ViewsModule::closeViewWithID(const std::string &uuid) {
  cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithID:  uuid = %s", uuid.c_str());

  if (uuid.empty()) {
    impl->_logger->log(cly::LogLevel::WARNING, "[ViewsModule] closeViewWithID: uuid can not be null or empty!");
    return;
  }

  if (impl->_viewsStartTime.find(uuid) == impl->_viewsStartTime.end()) {
    cly::LogLevel::INFO, cly::utils::format("[ViewsModule] closeViewWithID:  Couldn't found "
                                            "view with uuid = %s",
                                            uuid.c_str());
    return;
  }

  _closeView(uuid);
}

void ViewsModule::_closeView(std::string eventID) {
  std::chrono::system_clock::duration duration = std::chrono::system_clock::now() - impl->_viewsStartTime[eventID].startTime;
  std::map<std::string, std::string> viewSegments;

  viewSegments["_idv"] = eventID;
  viewSegments["name"] = impl->_viewsStartTime[eventID].name;
  impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration.count());
  impl->_viewsStartTime.erase(eventID);
}
} // namespace cly