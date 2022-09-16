#ifndef LOGGER_MODULE_HPP_
#define LOGGER_MODULE_HPP_
#include <functional>
#include <memory>
#include <string>

namespace cly {
enum class LogLevel { DEBUG = 1, INFO = 2, WARNING = 3, ERROR = 4, FATAL = 5 };
using LoggerFunction = std::function<void(LogLevel, const std::string &)>;

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

private:
  class LoggerModuleImpl;
  std::unique_ptr<LoggerModuleImpl> impl;
};
} // namespace cly
#endif
