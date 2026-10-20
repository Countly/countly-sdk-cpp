#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include "countly/request_builder.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace cly;
using namespace std;

/**
 * Validate request queue threshold.
 * Result: All the requests from the queue will remove and size of the 'RQPeekAll' list will be 0.
 * @param *storageModule: a pointer to the storage module.
 * @param *requestModule: a pointer to the request module.
 */
void ValidateRequestSizeOnReachingThresholdLimit(std::shared_ptr<StorageModuleBase> storageModule, std::shared_ptr<RequestModule> requestModule) {

  CHECK(storageModule->RQCount() == 0);

  std::map<std::string, std::string> data = {{"param1", "value1"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 1);

  data = {{"param2", "value2"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 2);

  data = {{"param3", "value3"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 3);

  data = {{"param4", "value4"}};
  requestModule->addRequestToQueue(data);

  CHECK(storageModule->RQCount() == 3);

  std::shared_ptr<DataEntry> frontRequest = storageModule->RQPeekFront();
  CHECK(frontRequest->getId() == 2);
  std::string requestData = frontRequest->getData();
  CHECK(requestData.find("app_key=") != std::string::npos);
  CHECK(requestData.find("device_id=") != std::string::npos);
  CHECK(requestData.find("param2=value2") != std::string::npos);
}

TEST_CASE("Test Request Module with Memory Storage") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
#ifdef COUNTLY_USE_SQLITE
  configuration->databasePath = TEST_DATABASE_NAME;
#endif
  configuration->requestQueueThreshold = 3;

  std::shared_ptr<StorageModuleMemory> storageModule = std::make_shared<StorageModuleMemory>(configuration, logger);
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);
  std::shared_ptr<RequestModule> requestModule = std::make_shared<RequestModule>(configuration, logger, requestBuilder, storageModule);
  std::shared_ptr<ConfigurationModule> configurationModule = std::make_shared<ConfigurationModule>(nullptr, configuration, logger, requestBuilder, storageModule, requestModule, std::make_shared<std::mutex>());

  requestModule->setConfigurationProvider(configurationModule);
  storageModule->init();
  configurationModule->fetchConfigFromStorage();

  SUBCASE("Validate request queue threshold") { ValidateRequestSizeOnReachingThresholdLimit(storageModule, requestModule); }
}

#ifdef COUNTLY_USE_SQLITE
TEST_CASE("Test Request Module with SQLite Storage") {
  test_utils::clearSDK();
  shared_ptr<cly::LoggerModule> logger;
  logger.reset(new cly::LoggerModule());

  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("", "");
  configuration->databasePath = TEST_DATABASE_NAME;
  configuration->requestQueueThreshold = 3;

  std::shared_ptr<StorageModuleDB> storageModule = std::make_shared<StorageModuleDB>(configuration, logger);
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);
  std::shared_ptr<RequestModule> requestModule = std::make_shared<RequestModule>(configuration, logger, requestBuilder, storageModule);
  std::shared_ptr<ConfigurationModule> configurationModule = std::make_shared<ConfigurationModule>(nullptr, configuration, logger, requestBuilder, storageModule, requestModule, std::make_shared<std::mutex>());

  requestModule->setConfigurationProvider(configurationModule);
  storageModule->init();
  configurationModule->fetchConfigFromStorage();

  SUBCASE("Validate request queue threshold") { ValidateRequestSizeOnReachingThresholdLimit(storageModule, requestModule); }
}
#endif

// Helper to parse a serialized request string into a key-value map
static std::map<std::string, std::string> parseRequest(const std::string &data) {
  std::map<std::string, std::string> result;
  std::string::size_type start = 0;
  while (start < data.size()) {
    auto ampersand = data.find('&', start);
    if (ampersand == std::string::npos) {
      ampersand = data.size();
    }
    auto eq = data.find('=', start);
    if (eq != std::string::npos && eq < ampersand) {
      result[data.substr(start, eq - start)] = data.substr(eq + 1, ampersand - eq - 1);
    }
    start = ampersand + 1;
  }
  return result;
}

TEST_CASE("Requests contain dow, hour, and tz") {
  shared_ptr<cly::LoggerModule> logger = std::make_shared<cly::LoggerModule>();
  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("test_app_key", "test_device_id");
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);

  SUBCASE("buildRequest includes dow, hour, and tz fields") {
    std::map<std::string, std::string> data = {{"test_key", "test_value"}};
    std::string request = requestBuilder->buildRequest(data);

    auto parsed = parseRequest(request);

    CHECK(parsed.find("dow") != parsed.end());
    CHECK(parsed.find("hour") != parsed.end());
    CHECK(parsed.find("tz") != parsed.end());
    CHECK(parsed.find("timestamp") != parsed.end());
  }

  SUBCASE("dow is between 0 and 6") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int dow = std::stoi(parsed["dow"]);
    CHECK(dow >= 0);
    CHECK(dow <= 6);
  }

  SUBCASE("hour is between 0 and 23") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int hour = std::stoi(parsed["hour"]);
    CHECK(hour >= 0);
    CHECK(hour <= 23);
  }

  SUBCASE("tz is a valid timezone offset in minutes") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    int tz = std::stoi(parsed["tz"]);
    // Valid timezone offsets range from UTC-12 (-720) to UTC+14 (+840)
    CHECK(tz >= -720);
    CHECK(tz <= 840);
  }

  SUBCASE("dow and hour match current local time") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);

    CHECK(std::stoi(parsed["dow"]) == local_tm.tm_wday);
    CHECK(std::stoi(parsed["hour"]) == local_tm.tm_hour);
  }

  SUBCASE("tz matches current timezone offset") {
    std::map<std::string, std::string> data;
    std::string request = requestBuilder->buildRequest(data);
    auto parsed = parseRequest(request);

    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);
    std::tm gm_tm = *std::gmtime(&now);

    int expected_tz = (local_tm.tm_hour - gm_tm.tm_hour) * 60 + (local_tm.tm_min - gm_tm.tm_min);
    int day_diff = local_tm.tm_mday - gm_tm.tm_mday;
    if (day_diff > 1) {
      day_diff = -1;
    } else if (day_diff < -1) {
      day_diff = 1;
    }
    expected_tz += day_diff * 24 * 60;

    CHECK(std::stoi(parsed["tz"]) == expected_tz);
  }
}

#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
namespace {
/**
 * HTTP server on 127.0.0.1 for exercising the SDK's built-in libcurl client.
 *
 * An answering server replies to every request with 200 and "{}" and records its
 * request line. A silent server accepts connections and never replies; it records
 * how long each connection stayed open until the client closed it.
 */
class LocalHTTPServer {
public:
  explicit LocalHTTPServer(bool answering) : _answering(answering) {
    _listener = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(_listener >= 0);
    int reuse = 1;
    ::setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(_listener, 16) == 0);

    socklen_t length = sizeof(address);
    REQUIRE(::getsockname(_listener, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    _port = ntohs(address.sin_port);
    _thread = std::thread(&LocalHTTPServer::serve, this);
  }

  ~LocalHTTPServer() { stop(); }

  LocalHTTPServer(const LocalHTTPServer &) = delete;
  LocalHTTPServer &operator=(const LocalHTTPServer &) = delete;

  /**
   * Closes every connection and the listening socket. A client still waiting for an answer gets an error instead.
   */
  void stop() {
    if (_stopping.exchange(true)) {
      return;
    }
    _thread.join();
    ::close(_listener);
  }

  int port() const { return _port; }

  /**
   * Request lines received so far, such as "GET /i?app_key=... HTTP/1.1".
   */
  std::vector<std::string> requestLines() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _request_lines;
  }

  /**
   * For each connection the client closed, how long it had stayed open.
   */
  std::vector<std::chrono::milliseconds> closedConnectionDurations() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _closed_durations;
  }

private:
  struct HeldConnection {
    int descriptor;
    std::chrono::steady_clock::time_point accepted;
  };

  void serve() {
    std::vector<HeldConnection> held;
    while (!_stopping.load()) {
      std::vector<pollfd> watched;
      watched.push_back({_listener, POLLIN, 0});
      for (const HeldConnection &connection : held) {
        watched.push_back({connection.descriptor, POLLIN, 0});
      }
      if (::poll(watched.data(), watched.size(), 50) <= 0) {
        continue;
      }

      std::vector<HeldConnection> still_open;
      for (size_t index = 0; index < held.size(); index++) {
        char buffer[4096];
        if (watched[index + 1].revents == 0 || ::recv(held[index].descriptor, buffer, sizeof(buffer), 0) > 0) {
          still_open.push_back(held[index]);
          continue;
        }
        ::close(held[index].descriptor);
        std::lock_guard<std::mutex> lock(_mutex);
        _closed_durations.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - held[index].accepted));
      }
      held.swap(still_open);

      if (watched[0].revents & POLLIN) {
        int client = ::accept(_listener, nullptr, nullptr);
        if (client >= 0 && _answering) {
          answer(client);
        } else if (client >= 0) {
          held.push_back({client, std::chrono::steady_clock::now()});
        }
      }
    }

    for (const HeldConnection &connection : held) {
      ::close(connection.descriptor);
    }
  }

  /**
   * Reads what the client sent, waiting at most five seconds for it. Returns what recv() returns, or -1 on a timeout.
   */
  static ssize_t receiveWithin5Seconds(int client, char *buffer, size_t size) {
    pollfd readable = {client, POLLIN, 0};
    if (::poll(&readable, 1, 5000) <= 0) {
      return -1;
    }
    return ::recv(client, buffer, size, 0);
  }

  void answer(int client) {
    std::string request;
    char buffer[4096];
    std::string::size_type header_end;
    while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
      ssize_t received = receiveWithin5Seconds(client, buffer, sizeof(buffer));
      if (received <= 0) {
        ::close(client);
        return;
      }
      request.append(buffer, static_cast<size_t>(received));
    }

    size_t content_length = 0;
    const std::string::size_type length_header = request.find("Content-Length: ");
    if (length_header != std::string::npos && length_header < header_end) {
      content_length = std::stoul(request.substr(length_header + 16));
    }
    while (request.size() < header_end + 4 + content_length) {
      ssize_t received = receiveWithin5Seconds(client, buffer, sizeof(buffer));
      if (received <= 0) {
        break;
      }
      request.append(buffer, static_cast<size_t>(received));
    }

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _request_lines.push_back(request.substr(0, request.find("\r\n")));
    }

    const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#ifdef MSG_NOSIGNAL
    ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
#else
    ::send(client, response.data(), response.size(), 0);
#endif
    ::close(client);
  }

  const bool _answering;
  int _listener = -1;
  int _port = 0;
  std::thread _thread;
  std::atomic<bool> _stopping{false};
  mutable std::mutex _mutex;
  std::vector<std::string> _request_lines;
  std::vector<std::chrono::milliseconds> _closed_durations;
};
} // namespace

TEST_CASE("Built-in HTTP client") {
  const std::string database_path = "test-countly-http.db";
  remove(database_path.c_str());

  SUBCASE("requests reach a server whose URL ends with a slash") {
    LocalHTTPServer server(true);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1/", server.port(), false);

    bool begin_session_received = false;
    int queued_requests = -1;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      countly->processRQDebug();
      for (const std::string &request_line : server.requestLines()) {
        if (request_line.rfind("GET /i?", 0) == 0 && request_line.find("begin_session=1") != std::string::npos) {
          begin_session_received = true;
        }
      }
      queued_requests = countly->checkRQSize();
      if (begin_session_received && queued_requests == 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    countly.reset();
    server.stop();
    remove(database_path.c_str());

    CHECK(begin_session_received);
    CHECK(queued_requests == 0);
  }

  SUBCASE("a request to a server that never answers is given up after the request timeout") {
    LocalHTTPServer server(false);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", server.port(), false);

    const std::chrono::seconds request_timeout(COUNTLY_HTTP_REQUEST_TIMEOUT_SECONDS);
    std::vector<std::chrono::milliseconds> open_durations;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + request_timeout + std::chrono::seconds(15);
    while (open_durations.empty() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      open_durations = server.closedConnectionDurations();
    }

    // The server goes first so that a client still waiting on it returns and the SDK can shut down
    server.stop();
    countly.reset();
    remove(database_path.c_str());

    REQUIRE(!open_durations.empty());
    CHECK(open_durations[0] >= request_timeout - std::chrono::seconds(1));
    CHECK(open_durations[0] <= request_timeout + std::chrono::seconds(5));
  }
}
#endif
