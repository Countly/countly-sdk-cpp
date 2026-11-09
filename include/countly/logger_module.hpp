#ifndef LOGGER_MODULE_HPP_
#define LOGGER_MODULE_HPP_
#include <functional>
#include <memory>
#include <string>

namespace cly {
enum class LogLevel { DEBUG = 1, INFO = 2, WARNING = 3, ERROR = 4, FATAL = 5 };
using LoggerFunction = std::function<void(LogLevel, const std::string &)>;
class HealthCheckModule;

class LoggerModule {
public:
  LoggerModule();
  ~LoggerModule();

  /**
   * Set custom logger function.
   *
   * @param logger pointer to function.
   */
  void setLogger(LoggerFunction logger);


  /**
   * Get custom logger function.
   */
  const LoggerFunction getLogger();

  /**
   * Print important information.
   *
   * @param level importance and urgency of the message.
   * @param message description of log.
   */
  void log(LogLevel level, const std::string &message);

  /**
   * @return true when the calling thread is currently inside the integrator's log
   * callback.
   *
   * The SDK invokes that callback from places that hold the instance mutex, so a
   * method which takes that mutex cannot serve a call made from inside the
   * callback -- it would deadlock. Methods that have a "cannot determine" return
   * value check this and use it instead of locking.
   */
  static bool isInsideCallback();

  /**
   * Hands the health check counters to the logger. From then on every WARNING is counted as a
   * warning and every ERROR or FATAL as an error, whether or not a log callback is set.
   *
   * @param healthCheck the counters of this instance
   */
  void setHealthCheck(std::shared_ptr<HealthCheckModule> healthCheck);

  /**
   * @return the health check counters of this instance, empty before they are set
   */
  std::shared_ptr<HealthCheckModule> healthCheck() const;

  /**
   * Records a health check log code, such as "w740", next to the log line of its condition.
   *
   * @param code the log code
   * @param count how many times the condition occurred
   */
  void recordLogCode(const std::string &code, int count = 1);

  /**
   * Counts one call of a method for the health check method usage.
   *
   * @param area the feature area: "sessions", "events", "views" or "crashes"
   * @param method the method name, with ":a" for a call the SDK made by itself
   */
  void recordUsage(const std::string &area, const std::string &method);

private:
  class LoggerModuleImpl;
  std::unique_ptr<LoggerModuleImpl> impl;
};
} // namespace cly
#endif
