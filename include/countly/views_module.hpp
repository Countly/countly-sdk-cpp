#ifndef VIEWS_MODULE_HPP_
#define VIEWS_MODULE_HPP_
#include <map>
#include <memory>
#include <string>

#include "countly/configuration_provider.hpp"
#include "countly/constants.hpp"
#include "countly/logger_module.hpp"

namespace cly {
class ConsentModule;
class Event;

class ViewsModule {

public:
  ~ViewsModule();
  ViewsModule(cly::CountlyDelegates *cly, std::shared_ptr<cly::LoggerModule> logger);

  /**
   * Close view with id.
   *
   * @param viewId: id of the view.
   * @param segmentation: custom data added to the event that ends the view.
   */
  void closeViewWithID(const std::string &viewId, const std::map<std::string, std::string> &segmentation = {});

  /**
   * Close view with name. When several running views have that name, the one started last is closed.
   *
   * @param name: name of the view.
   * @param segmentation: custom data added to the event that ends the view.
   */
  void closeViewWithName(const std::string &name, const std::map<std::string, std::string> &segmentation = {});

  /**
   * Open view with name.
   *
   * @param name: name of the view.
   * @param segmentation: custom data you want to set.
   */
  std::string openView(const std::string &name, const std::map<std::string, std::string> &segmentation = {});

  /**
   * Closes every running view, each with its own duration.
   *
   * @param segmentation: custom data added to the event that ends each view.
   */
  void stopAllViews(const std::map<std::string, std::string> &segmentation = {});

private:
  friend class Countly;
  class ViewModuleImpl;
  std::unique_ptr<ViewModuleImpl> impl;
  void setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider); // try injecting

  /**
   * Sets the consent state the view calls check for the "views" consent.
   */
  void setConsentModule(std::shared_ptr<ConsentModule> consent);

  /**
   * Ends every running view and records its end event, while the SDK behavior settings allow view tracking;
   * otherwise the views are dropped without an event. Not counted as a call of the app.
   */
  void stopRunningViews();

  /**
   * Lets the next view started while a session runs be the first view of that session.
   */
  void resetFirstView();

  /**
   * Forgets the current and the previous view, after the running views were stopped for another user.
   */
  void forgetViews();

  /**
   * The ID of the view started last, sent as "cvid" with other events. Empty before the first view.
   */
  std::string currentViewId();

  /**
   * Sets an event-level field of a view event, such as the view ID.
   */
  static void setEventField(cly::Event &event, const std::string &key, const std::string &value);
};
} // namespace cly
#endif
