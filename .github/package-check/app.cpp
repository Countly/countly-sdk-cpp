#include "countly.hpp"
#include <iostream>

#if defined(EXPECT_COUNTLY_SQLITE) && !defined(COUNTLY_USE_SQLITE)
#error "COUNTLY_USE_SQLITE did not reach the app through find_package"
#endif

int main() {
  cly::Countly &countly = cly::Countly::getInstance();
  countly.setHTTPClient([](bool use_post, const std::string &path, const std::string &data) {
    (void)use_post;
    (void)path;
    (void)data;
    return cly::HTTPResponse{true, nlohmann::json::object()};
  });
#ifdef EXPECT_COUNTLY_SQLITE
  countly.setDatabasePath("install_check.db");
#endif
  countly.setDeviceID("install-check-device");
  countly.start("install-check-app-key", "https://example.invalid", 443, false);

  cly::Event event("install_check", 1);
  event.addSegmentation("source", "installed package");
  countly.addEvent(event);
  const bool started = countly.checkEQSize() >= 0;
  std::cout << event.serialize() << (started ? " recorded" : " not recorded: the SDK did not start") << std::endl;

  countly.stop();
  return started ? 0 : 1;
}
