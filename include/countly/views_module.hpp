#ifndef VIEWS_MODULE_HPP_
#define VIEWS_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/constants.hpp"
#include "countly/logger_module.hpp"

namespace cly {
class ViewsModule {

public:
  class Event;
  class LoggerModule;
  ~ViewsModule();
  ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger);

  /**
   * Close view a with id.
   *
   * @param uuid: id of the view.
   */
  void closeViewWithID(const std::string &uuid);

  /**
   * Close view a with name.
   *
   * @param name: name of the view.
   */
  void closeViewWithName(const std::string &name);

  /**
   * Close view a with name.
   *
   * @param name: name of the view.
   * @param segmentation: custom data you want to set.
   */
  std::string openView(const std::string &name, std::map<std::string, std::string> segmentation = {});

private:
  class ViewModuleImpl;
  std::unique_ptr<ViewModuleImpl> impl;
};
} // namespace cly
#endif
