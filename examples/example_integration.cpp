#include "countly.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

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

// // Callback function to write response data
// static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
//     ((std::string*)userp)->append((char*)contents, size * nmemb);
//     return size * nmemb;
// }

// // Custom HTTP client for macOS
// HTTPResponse customClient(bool use_post, const std::string &path, const std::string &data) {
//   HTTPResponse response;
//   response.success = false;
//   cout << "Making real HTTP request to: " << path << endl;

//   CURL *curl;
//   CURLcode res;
//   std::string readBuffer;

//   curl = curl_easy_init();
//   if(curl) {
//     // Set URL
//     curl_easy_setopt(curl, CURLOPT_URL, path.c_str());
    
//     // Set callback function to write data
//     curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//     curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    
//     // Set timeout
//     curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
//     // Follow redirects
//     curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
//     // SSL verification (set to 0 for testing, 1 for production)
//     curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
//     curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
//     // Set User-Agent
//     curl_easy_setopt(curl, CURLOPT_USERAGENT, "Countly-SDK-CPP/1.0");
    
//     if (use_post) {
//       // Set POST method
//       curl_easy_setopt(curl, CURLOPT_POST, 1L);
      
//       if (!data.empty()) {
//         // Set POST data
//         curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
//         curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
//       }
      
//       // Set content type for POST
//       struct curl_slist *headers = NULL;
//       headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
//       curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
//     } else {
//       // For GET requests, append data as query parameters
//       if (!data.empty()) {
//         std::string fullUrl = path;
//         fullUrl += (path.find('?') != std::string::npos) ? "&" : "?";
//         fullUrl += data;
//         curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
//       }
//     }
    
//     // Perform the request
//     res = curl_easy_perform(curl);
    
//     if(res != CURLE_OK) {
//       cout << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
//     } else {
//       // Get response code
//       long response_code;
//       curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
      
//       cout << "HTTP Response Code: " << response_code << endl;
//       cout << "Raw response: " << readBuffer << endl;
      
//       // Check if HTTP request was successful (2xx status codes)
//       if (response_code >= 200 && response_code < 300) {
//         // Check if response contains { result: 'Success' } or {"result":"Success"}
//         if (readBuffer.find("\"result\"") != std::string::npos && 
//             (readBuffer.find("\"Success\"") != std::string::npos || 
//              readBuffer.find("'Success'") != std::string::npos)) {
//           response.success = true;
//           cout << "Success response detected!" << endl;
//         } else {
//           cout << "Response does not indicate success" << endl;
//         }
        
//         // Parse as JSON
//         try {
//           response.data = nlohmann::json::parse(readBuffer);
//         } catch (const std::exception& e) {
//           cout << "Failed to parse JSON response: " << e.what() << endl;
//           response.data = nlohmann::json::object();
//         }
//       } else {
//         cout << "HTTP request failed with code: " << response_code << endl;
//       }
//     }
    
//     // Cleanup
//     curl_easy_cleanup(curl);
//   } else {
//     cout << "Failed to initialize curl" << endl;
//   }

//   cout << "Request completed. Success: " << (response.success ? "true" : "false") << endl;
//   return response;
// }

// ---------------------------------------------------------------------------
// Multi instance support
//
// The SDK keeps a process wide registry of named instances:
//   * the unnamed instance is the default one, returned by Countly::getInstance()
//   * any other instance is created with Countly::createInstance(name) and later
//     looked up with Countly::getInstance(name) or Countly::findInstance(name)
//
// Each instance is fully independent: its own app key, device id, session,
// event queue, request queue, background thread and modules. Two rules matter
// when you run more than one:
//   1. Give every instance its own app key.
//   2. On SQLite builds give every instance its own database file. start()
//      refuses a path that another live instance has already claimed, and the
//      claim is only released when that instance is destroyed (not on stop()).
// ---------------------------------------------------------------------------
static const string SERVER_URL = "https://your.server.ly";
static const int SERVER_PORT = 443;

static const string PRIMARY_APP_KEY = "YOUR_APP_KEY";
static const string PRIMARY_DEVICE_ID = "test-device-id";
static const string PRIMARY_DB_PATH = "databaseFileName.db";

// The name is just a registry key, it is never sent to the server.
static const string SECONDARY_INSTANCE_NAME = "secondary";
static const string SECONDARY_APP_KEY = "YOUR_SECOND_APP_KEY";
static const string SECONDARY_DEVICE_ID = "test-device-id-2";
static const string SECONDARY_DB_PATH = "databaseFileName2.db";

// Applies the same set of configurations to any instance and starts it.
// Note that every setter is called on the instance it belongs to: there is no
// "current" instance in the SDK, so a call on the default instance never
// configures a named one.
static void configureAndStart(Countly &instance, const string &appKey, const string &deviceId, const string &dbPath) {
  // All configurations below are put here as an example
  // Your configuration in your app may be different
  // Please refer to the documentation for more information:
  // https://support.count.ly/hc/en-us/articles/4416163384857-C-

  // Custom HTTP client
  // HTTPClientFunction clientPtr = customClient;
  // instance.setHTTPClient(clientPtr);
  // instance.alwaysUsePost(true);
  instance.setLogger(printLog);
  instance.SetPath(dbPath); // this will be only built into account if the correct configurations are set
  instance.setDeviceID(deviceId);
  // instance.setSalt("test-salt");
  // OS, OS_version, device, resolution, carrier, app_version);
  instance.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");

  instance.setAutomaticSessionUpdateInterval(5); // The value is set so low just for internal validation. Has to be set before start.
  instance.setMaxRQProcessingBatchSize(2);       // in most cases not needed to be set. The value is set so low just for internal validation

  // start the SDK (initialize the SDK)
  instance.start(appKey, SERVER_URL, SERVER_PORT, true);
}

// Creates and starts the secondary instance. createInstance returns the existing
// instance (and logs a warning) if the name is already taken, so this is safe to
// call twice.
static Countly &startSecondaryInstance() {
  Countly &second = Countly::createInstance(SECONDARY_INSTANCE_NAME);
  configureAndStart(second, SECONDARY_APP_KEY, SECONDARY_DEVICE_ID, SECONDARY_DB_PATH);
  return second;
}

// Always resolve a named instance through findInstance instead of caching the
// reference: destroyInstance() frees the object, and any reference or pointer
// kept across that call dangles.
static Countly *secondaryInstance() {
  Countly *second = Countly::findInstance(SECONDARY_INSTANCE_NAME);
  if (second == nullptr) {
    printLog(LogLevel::WARNING, "[ExampleIntegration] The secondary instance does not exist, create it first");
  }
  return second;
}

// Hammers both instances from several threads at once. Each instance has its own
// lock, so the two do not contend with each other; within one instance the calls
// below are internally synchronised.
//
// Views are deliberately driven from a single thread per instance: ViewsModule
// keeps its open-view map without a lock of its own, so concurrent openView /
// closeView calls on the *same* instance are not safe. Two threads driving views
// on two *different* instances are, since the modules are per instance.
static void stressBothInstances(int threadsPerInstance, int iterations) {
  Countly *second = secondaryInstance();
  if (second == nullptr) {
    return;
  }

  Countly *targets[2] = {&Countly::getInstance(), second};
  const char *labels[2] = {"primary", "secondary"};

  std::vector<std::thread> workers;
  std::atomic<int> recorded(0);

  for (int t = 0; t < 2; t++) {
    Countly *target = targets[t];
    const string label = labels[t];

    for (int w = 0; w < threadsPerInstance; w++) {
      workers.emplace_back([target, label, w, iterations, &recorded]() {
        for (int i = 0; i < iterations; i++) {
          target->RecordEvent("stress_basic_" + label, 1);

          std::map<std::string, std::string> segmentation = {
              {"instance", label},
              {"worker", std::to_string(w)},
              {"iteration", std::to_string(i)},
          };
          target->RecordEvent("stress_segmented_" + label, segmentation, 1, 2.5, 0.5);

          target->crash().addBreadcrumb(label + "-" + std::to_string(w) + "-" + std::to_string(i));

          if (i % 5 == 0) {
            target->updateSession();
          }

          recorded.fetch_add(2);
        }
      });
    }

    // One view thread per instance, see the note above.
    workers.emplace_back([target, label, iterations]() {
      for (int i = 0; i < iterations; i++) {
        const std::string viewId = target->views().openView("stress view " + label);
        if (!viewId.empty()) {
          target->views().closeViewWithID(viewId);
        }
      }
    });
  }

  for (std::thread &worker : workers) {
    worker.join();
  }

  printLog(LogLevel::INFO, "[ExampleIntegration] Stress finished, events recorded = " + std::to_string(recorded.load()) + ", primary EQ = " + std::to_string(Countly::getInstance().checkEQSize()) + ", secondary EQ = " + std::to_string(second->checkEQSize()));
}

static void printInstanceStatus() {
  Countly *second = Countly::findInstance(SECONDARY_INSTANCE_NAME);
  cout << "Default instance   : initialized, RQ size = " << Countly::getInstance().checkRQSize() << ", EQ size = " << Countly::getInstance().checkEQSize() << endl;
  cout << "Instance '" << SECONDARY_INSTANCE_NAME << "': " << (second == nullptr ? "not created" : "present") << endl;
  if (second != nullptr) {
    cout << "                     RQ size = " << second->checkRQSize() << ", EQ size = " << second->checkEQSize() << endl;
  }
  cout << "hasInstance(\"" << SECONDARY_INSTANCE_NAME << "\") = " << (Countly::hasInstance(SECONDARY_INSTANCE_NAME) ? "true" : "false") << endl;
  // getInstance("") is the default instance, so this is always true.
  cout << "getInstance(\"\") == getInstance() : " << (&Countly::getInstance("") == &Countly::getInstance() ? "true" : "false") << endl;
}

int main() {
  cout << "Sample App" << endl;

  if (PRIMARY_APP_KEY.compare("YOUR_APP_KEY") == 0 || SERVER_URL.compare("https://your.server.ly") == 0) {
    printLog(LogLevel::WARNING, "[ExampleIntegration] Please do not use default set of app key and server url");
  }

  // The default (unnamed) instance.
  Countly &ct = Countly::getInstance();
  configureAndStart(ct, PRIMARY_APP_KEY, PRIMARY_DEVICE_ID, PRIMARY_DB_PATH);

  // A second, independent instance reporting to a second app.
  startSecondaryInstance();

  ct.crash().addBreadcrumb("start");

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
    cout << "12) Leave breadcrumb" << endl;
    cout << "13) Record a crash with bread crumbs and segmentation" << endl;
    cout << "-- multi instance --" << endl;
    cout << "14) Basic event on the secondary instance" << endl;
    cout << "15) Same event on both instances" << endl;
    cout << "16) Record a view on both instances" << endl;
    cout << "17) Concurrency stress on both instances" << endl;
    cout << "18) Instance registry status" << endl;
    cout << "19) Destroy the secondary instance" << endl;
    cout << "20) Create and start the secondary instance again" << endl;
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

      // Call the setter on the instance you mean. 'ct.getInstance()' would work
      // here only because 'ct' happens to be the default instance.
      ct.setUserDetails(userdetail);
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
    case 12: {
      const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
      const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

      ct.crash().addBreadcrumb(std::to_string(timestamp.count()));
    } break;
    case 13: {
      std::map<std::string, std::string> segmentation = {
          {"platform", "ubuntu"},
          {"time", "60"},
      };

      std::map<std::string, std::string> crashMetrics = {
          {"_run", "199222"}, {"_app_version", "1.0"}, {"_disk_current", "654321"}, {"_disk_total", "10585852"}, {"_os", "windows"},
      };

      ct.crash().recordException("Divided by zero", "stack trace", true, crashMetrics, segmentation);
    } break;
    case 14: {
      if (Countly *second = secondaryInstance()) {
        second->RecordEvent("Event on the secondary instance", 1);
      }
    } break;
    case 15: {
      // The same key on both instances. Each event stays in the queue of the
      // instance it was recorded on and is sent with that instance's app key.
      std::map<std::string, std::string> segmentation = {{"source", "menu 15"}};
      ct.RecordEvent("Event on both instances", segmentation, 1);
      if (Countly *second = secondaryInstance()) {
        second->RecordEvent("Event on both instances", segmentation, 1);
      }
    } break;
    case 16: {
      Countly *second = secondaryInstance();
      // View ids are unique per view, so the two instances report different ids
      // for the same view name.
      const std::string primaryViewId = ct.views().openView("Shared view name");
      const std::string secondaryViewId = second != nullptr ? second->views().openView("Shared view name") : "";
      cout << "primary view id   = " << primaryViewId << endl;
      cout << "secondary view id = " << secondaryViewId << endl;

      std::this_thread::sleep_for(2s);

      ct.views().closeViewWithID(primaryViewId);
      if (second != nullptr && !secondaryViewId.empty()) {
        second->views().closeViewWithID(secondaryViewId);
      }
    } break;
    case 17:
      stressBothInstances(4, 25);
      break;
    case 18:
      printInstanceStatus();
      break;
    case 19:
      // Ends the instance's session, joins its threads and frees its database
      // path claim. Any reference to it dangles afterwards.
      Countly::destroyInstance(SECONDARY_INSTANCE_NAME);
      printLog(LogLevel::INFO, "[ExampleIntegration] Secondary instance destroyed");
      break;
    case 20:
      startSecondaryInstance();
      break;
    case 0:
      flag = false;
      break;
    default:
      printLog(LogLevel::DEBUG, "[ExampleIntegration] Please do not use default set of app key and server url");
      break;
    }
  }

  // Stop every instance, then let the registry destroy them. stop() ends the
  // session and joins the update thread; destroyAllInstances() also frees the
  // database path claims. 'ct' must not be touched after that call, it refers to
  // a destroyed object.
  if (Countly *second = Countly::findInstance(SECONDARY_INSTANCE_NAME)) {
    second->stop();
  }
  ct.stop();
  Countly::destroyAllInstances();

  return 0;
}
