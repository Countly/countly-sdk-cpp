#ifndef STORAGE_MODULE_HPP_
#define STORAGE_MODULE_HPP_
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include <deque>
#include <memory>
#include <string>

namespace cly {

class StorageModule {
private:
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::deque<std::string> request_queue;

public:
  StorageModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger);
  ~StorageModule();

 void init();
 std::string *RQPeekAll();

 void RQClearAll(); // done
 void RQInsertAtEnd(const std::string &request); //done
 std::string& RQPeekFront(); // done
 void RQRemoveFront(); //done
 int RQCount(); //done

};
} // namespace cly
#endif