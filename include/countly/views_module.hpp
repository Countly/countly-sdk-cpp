#ifndef VIEWS_MODULE_HPP_
#define VIEWS_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/configuration_provider.hpp"
#include "countly/constants.hpp"
#include "countly/logger_module.hpp"

namespace cly {
class ViewsModule {

public:
  ~ViewsModule();
  ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger);

  /**
   * Close view with id.
   *
   * @param viewId: id of the view.
   */
  void closeViewWithID(const std::string &viewId);

  /**
   * Close view with name.
   *
   * @param name: name of the view.
   */
  void closeViewWithName(const std::string &name);

  /**
   * Open view with name.
   *
   * @param name: name of the view.
   * @param segmentation: custom data you want to set.
   */
  std::string openView(const std::string &name, const std::map<std::string, std::string> &segmentation = {});


private:
  friend class Countly;
  void _recordView(std::string eventID);
  class ViewModuleImpl;
  std::unique_ptr<ViewModuleImpl> impl;
  void setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider); // try injecting
};
} // namespace cly
#endif
