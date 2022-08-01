#include "countly/views_module.hpp"

#include <chrono>

#define CLY_VIEW_KEY "[CLY]_view"

namespace cly {
class ViewsModule::ViewModuleImpl {
public:
  ViewModuleImpl(cly::CountlyDelegates *cly,
                 std::shared_ptr<cly::LoggerModule> logger)
      : _cly{cly}, _logger{logger} {}

  bool _isFirstView = true;
  std::map<std::string, std::chrono::system_clock::time_point> _viewsStartTime;

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

void ViewsModule::recordOpenView(
    const std::string &name, std::map<std::string, std::string> segmentation) {
  impl->_logger->log(
      cly::LogLevel::INFO,
      cly::Utils::format(
          "[ViewsModule] recordOpenView:  name = {}, segmentation = {}", name,
          segmentation));

  if (impl->_viewsStartTime.find(name) == impl->_viewsStartTime.end()) {
    impl->_viewsStartTime[name] = std::chrono::system_clock::now();
  }

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

void ViewsModule::recordCloseView(const std::string &name) {
  if (impl->_viewsStartTime.find(name) != impl->_viewsStartTime.end()) {
    std::chrono::system_clock::duration duration =
        std::chrono::system_clock::now() - impl->_viewsStartTime[name];

    std::map<std::string, std::string> viewSegments;

    viewSegments["name"] = name;
    // viewSegments["segment"] = "cpp";

    impl->_cly->RecordEvent(CLY_VIEW_KEY, viewSegments, 1, 0, duration.count());
    impl->_viewsStartTime.erase(name);
  } else {
    impl->_logger->log(
        cly::LogLevel::INFO,
        cly::Utils::format("[ViewsModule] recordOpenView:  name = {}", name));
  }
}
} // namespace cly