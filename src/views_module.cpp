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

  ViewModuleImpl(cly::CountlyDelegates *cly,
                 std::shared_ptr<cly::LoggerModule> logger)
      : _cly{cly}, _logger{logger} {}
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

ViewsModule::ViewsModule(cly::CountlyDelegates *cly,
                         std::shared_ptr<cly::LoggerModule> logger) {
  impl.reset(new ViewModuleImpl(cly, logger));

  impl->_logger->log(cly::LogLevel::DEBUG,
                     cly::Utils::format("[ViewsModule] Initialized"));
}

ViewsModule::~ViewsModule() { impl->_logger.reset(); }

void ViewsModule::openView(const std::string &name,
                           std::map<std::string, std::string> segmentation) {
  impl->_logger->log(
      cly::LogLevel::INFO,
      cly::Utils::format("[ViewsModule] recordOpenView:  name = %s", name));

  if (name.empty()) {
    /*cly::Utils::format(
   "[ViewsModule] recordOpenView:  name = {}, segmentation = {}", name,
   segmentation));*/
    return;
  }

  std::string &uuid = cly::Utils::generateUUID();
  ViewModuleImpl::ViewInfo v;
  v.name = name;
  v.startTime = std::chrono::system_clock::now();

  impl->_viewsStartTime[uuid] = v;

  std::map<std::string, std::string> viewSegments;

  viewSegments["name"] = name;
  viewSegments["segment"] = "cpp";
  viewSegments["visit"] = 1;
  viewSegments["start"] = impl->_isFirstView ? 1 : 0;

  for (auto key_value : segmentation) {
    auto itr = viewSegments.find(key_value.first);
    if (itr != viewSegments.end()) {
      (*itr).second = key_value.second;
    } else {
      viewSegments[key_value.first] = key_value.second;
    }
  }

  impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1);
}

void ViewsModule::closeViewWithName(const std::string &name) {
  cly::Utils::format("[ViewsModule] recordOpenView:  name = {}", name);
  if (name.empty()) {
    // TODO:
    return;
  }

  std::string &uuid = impl->findViewUUIDByName(name);
  if (uuid.empty()) {
    // TODO:
    return;
  }

  std::chrono::system_clock::duration duration =
      std::chrono::system_clock::now() - impl->_viewsStartTime[uuid].startTime;

  std::map<std::string, std::string> viewSegments;

  viewSegments["name"] = name;
  // viewSegments["segment"] = "cpp";

  impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration.count());
  impl->_viewsStartTime.erase(name);
}

void ViewsModule::closeViewWithID(const std::string &uuid) {
  impl->_logger->log(
      cly::LogLevel::INFO,
      cly::Utils::format("[ViewsModule] recordOpenView:  uuid = {}", uuid));

  if (uuid.empty()) {
    /*cly::Utils::format(
   "[ViewsModule] recordOpenView:  name = {}, segmentation = {}", name,
   segmentation));*/
    return;
  }

  if (impl->_viewsStartTime.find(uuid) == impl->_viewsStartTime.end()) {
    // TODO:
    return;
  }

  std::chrono::system_clock::duration duration =
      std::chrono::system_clock::now() - impl->_viewsStartTime[uuid].startTime;

  std::map<std::string, std::string> viewSegments;

  viewSegments["name"] = impl->_viewsStartTime[uuid].name;
  // viewSegments["segment"] = "cpp";

  impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration.count());
  impl->_viewsStartTime.erase(uuid);
}
} // namespace cly