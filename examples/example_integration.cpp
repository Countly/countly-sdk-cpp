#include "countly.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

using namespace std;
using namespace cly;

void printLog(LogLevel level, const string &msg) {
  string lvl = "[DEBUG]";
  switch (level) {
  case LogLevel::DEBUG:
    lvl = "[Debug]";
    break;
  case LogLevel::INFO:
    lvl = "[INFO]";
    break;
  case LogLevel::WARNING:
    lvl = "[WARNING]";
    break;
  case LogLevel::FATAL:
    lvl = "[FATAL]";
    break;
  default:
    lvl = "[ERROR]";
    break;
  }

  cout << lvl << msg << endl;
}

int main() {
  cout << "Sample App" << endl;
  Countly &ct = Countly::getInstance();
  ct.alwaysUsePost(true);
  ct.setDeviceID("test-device-id");

  ct.setLogger(printLog);
  // OS, OS_version, device, resolution, carrier, app_version);
  ct.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
  // Server and port
  ct.start("YOUR_APP_KEY", "https://try.count.ly", 443, true);
  ct.SetMaxEventsPerMessage(10);
  ct.setAutomaticSessionUpdateInterval(5);

  bool flag = true;
  while (flag) {
    cout << "Choose your option:" << endl;
    cout << "1) Basic Event" << endl;
    cout << "2) Event with count and sum" << endl;
    cout << "3) Event with count, sum, duration" << endl;
    cout << "4) Event with sum, count, duration and segmentation" << endl;
    cout << "5) Update Session" << endl;
    cout << "6) Download remote config" << endl;
    cout << "7) Send user detail to server" << endl;
    cout << "8) Change device id with server merge" << endl;
    cout << "9) Change device id without server merge" << endl;
    cout << "10) Set user location" << endl;
    cout << "11) Record a view" << endl;
    cout << "0) Exit" << endl;
    int a;
    cin >> a;
    switch (a) {
    case 1:
      ct.RecordEvent("[CLY]_view", 123);
      break;
    case 2:
      ct.RecordEvent("Event with count and sum", 644, 13.3);
      break;
    case 3: {
      Event event("Event with sum, count, duration", 1, 10, 60.5);
      ct.addEvent(event);
      break;
    }
    case 4: {
      std::map<std::string, std::string> segmentation;
      segmentation["name"] = "start and end";
      ct.RecordEvent("Event with segmentation, count and sum", segmentation, 1, 0, 10);
      break;
    }
    case 5:
      ct.updateSession();
      break;
    case 6: {
      ct.updateRemoteConfig();
      break;
    }
    case 7: {
      std::map<std::string, std::string> userdetail = {
          {"name", "Full name"}, {"username", "username123"}, {"email", "useremail@email.com"}, {"phone", "222-222-222"}, {"phone", "222-222-222"}, {"picture", "http://webresizer.com/images2/bird1_after.jpg"}, {"gender", "M"}, {"byear", "1991"}, {"organization", "Organization"},
      };

      ct.getInstance().setUserDetails(userdetail);
    } break;
    case 8:
      ct.setDeviceID("new-device-id", true);
      break;
    case 9:
      ct.setDeviceID("new-device-id", false);
      break;
    case 10: {
      string countryCode = "us";
      string city = "Houston";
      string latitude = "29.634933";
      string longitude = "-95.220255";
      string ipAddress = "192.168.0.1";

      ct.setLocation(countryCode, city, latitude + "," + longitude, ipAddress);
    } break;

    case 11: {
      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };

      // Open a view
      std::string viewID = ct.views().openView("Main view", segmentation);

      std::this_thread::sleep_for(2s);

      // Close an opened view
      ct.views().closeViewWithID(viewID);
    } break;
    case 0:
      flag = false;
      break;
    default:
      cout << "Option not found!" << endl;
      break;
    }
  }

  ct.stop();

  return 0;
}