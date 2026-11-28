#include "countly/storage_module_base.hpp"
#include "countly/storage_module_db.hpp"
#include "countly/storage_module_memory.hpp"
#include "countly/request_builder.hpp"
#include "test_utils.hpp"

#include "doctest.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef COUNTLY_USE_CUSTOM_HTTP
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
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

// Helper to parse a serialized request string into a map of decoded values
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
      std::string value = data.substr(eq + 1, ampersand - eq - 1);
      test_utils::decodeURL(value);
      result[data.substr(start, eq - start)] = value;
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

/**
 * The name the server and dashboard know the C++ SDK by, spelled out rather than taken from COUNTLY_SDK_NAME so that a rename fails these tests.
 */
static const std::string EXPECTED_SDK_NAME = "cpp-native-unknown";

TEST_CASE("Requests contain the SDK name and version") {
  shared_ptr<cly::LoggerModule> logger = std::make_shared<cly::LoggerModule>();
  shared_ptr<cly::CountlyConfiguration> configuration = std::make_shared<CountlyConfiguration>("test_app_key", COUNTLY_TEST_HOST);
  configuration->deviceId = "test_device_id";
  std::shared_ptr<RequestBuilder> requestBuilder = std::make_shared<RequestBuilder>(configuration, logger);

  SUBCASE("a queued request carries them") {
    auto parsed = parseRequest(requestBuilder->buildRequest({}));

    // These follow the clock and are checked in "Requests contain dow, hour, and tz"
    for (const char *clock_key : {"timestamp", "dow", "hour", "tz"}) {
      CHECK(parsed.erase(clock_key) == 1);
    }
    CHECK(parsed == std::map<std::string, std::string>{{"app_key", "test_app_key"}, {"device_id", "test_device_id"}, {"t", "0"}, {"sdk_name", EXPECTED_SDK_NAME}, {"sdk_version", COUNTLY_SDK_VERSION}});
  }

  SUBCASE("a fetch request carries them and gains no other parameter") {
    std::map<std::string, std::string> data = {{"method", "sc"}, {"app_key", "test_app_key"}, {"device_id", "test_device_id"}};
    std::map<std::string, std::string> expected = data;
    expected["sdk_name"] = EXPECTED_SDK_NAME;
    expected["sdk_version"] = COUNTLY_SDK_VERSION;

    CHECK(parseRequest(requestBuilder->buildFetchRequest(data)) == expected);
  }

  SUBCASE("a request's own sdk_name and sdk_version do not replace the SDK's") {
    std::map<std::string, std::string> data = {{"sdk_name", "another-sdk"}, {"sdk_version", "1.0.0"}};

    auto queued = parseRequest(requestBuilder->buildRequest(data));
    auto fetch = parseRequest(requestBuilder->buildFetchRequest(data));

    CHECK(queued["sdk_name"] == EXPECTED_SDK_NAME);
    CHECK(queued["sdk_version"] == COUNTLY_SDK_VERSION);
    CHECK(fetch["sdk_name"] == EXPECTED_SDK_NAME);
    CHECK(fetch["sdk_version"] == COUNTLY_SDK_VERSION);
  }
}

/**
 * Waits up to five seconds for the fake HTTP client to receive a request carrying key=value.
 */
static bool waitForRequestWith(const std::string &key, const std::string &value) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!test_utils::httpQueueContains(key, value)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

/**
 * What a request is for: the method of a fetch from /o/sdk, otherwise the type of queued request it carries, or "hc" for the health check.
 */
static std::string requestKind(const test_utils::HTTPCall &call) {
  if (call.url == "/o/sdk") {
    std::map<std::string, std::string>::const_iterator method = call.data.find("method");
    if (method == call.data.end()) {
      return "";
    }
    return method->second;
  }
  // end_session comes before session_duration because an end_session request carries both
  for (const char *kind : {"begin_session", "end_session", "session_duration", "events", "user_details", "location", "crash", "old_device_id", "hc"}) {
    if (call.data.count(kind) > 0) {
      return kind;
    }
  }
  return "";
}

TEST_CASE("Every request carries the SDK name and version") {
  test_utils::clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(test_utils::fakeSendHTTP);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  // the session calls below work only with manual session control
  countly.enableManualSessionControl();
  countly.SetPath(TEST_DATABASE_NAME);
  // Only processRQDebug() sends: the update loop sleeps through the test, and immediate stop still ends it at once
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(60000);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  // Both fetches run on threads of their own, so each is awaited to keep the order fixed
  REQUIRE(waitForRequestWith("method", "sc"));
  countly.updateRemoteConfig();
  REQUIRE(waitForRequestWith("method", "rc"));

  countly.beginSession();
  countly.addEvent(cly::Event("click", 1));
  countly.updateSession();
  countly.setUserDetails({{"name", "Jane Doe"}});
  countly.setLocation("TR", "Izmir", "38.42,27.14", "");
  countly.crash().recordException("Error", "stack trace", false, {{"_os", "Linux"}, {"_app_version", "1.0"}});
  countly.setDeviceID("merged-device-id", true);
  countly.endSession();
  countly.processRQDebug();
  // once the merge request was delivered, the settings are fetched for the merged user
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (test_utils::http_call_queue.size() < 11 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::vector<std::string> queued;
  std::vector<std::string> fetches;
  size_t merge_index = 0;
  size_t last_fetch_index = 0;
  for (size_t index = 0; index < test_utils::http_call_queue.size(); index++) {
    test_utils::HTTPCall call = test_utils::http_call_queue.at(index);
    const std::string kind = requestKind(call);
    if (call.url == "/o/sdk") {
      fetches.push_back(kind);
      last_fetch_index = index;
    } else {
      queued.push_back(kind);
    }
    if (kind == "old_device_id") {
      merge_index = index;
    }
    CHECK(call.data["sdk_name"] == EXPECTED_SDK_NAME);
    CHECK(call.data["sdk_version"] == COUNTLY_SDK_VERSION);
  }
  CHECK(queued == std::vector<std::string>{"begin_session", "session_duration", "events", "user_details", "location", "crash", "old_device_id", "end_session"});
  // the fetch for the merged user runs on its own thread, so only its place after the merge request is fixed
  CHECK(fetches == std::vector<std::string>{"sc", "rc", "sc"});
  CHECK(last_fetch_index > merge_index);
}

/**
 * While set, sendHTTPToFlakyServer fails every request, as a server answering with an error or an unreachable one does.
 */
static std::atomic<bool> server_fails_requests(false);

/**
 * Fake HTTP client that records every request as fakeSendHTTP does, and fails each one while server_fails_requests is set.
 */
static HTTPResponse sendHTTPToFlakyServer(bool use_post, const std::string &url, const std::string &data) {
  HTTPResponse response = test_utils::fakeSendHTTP(use_post, url, data);
  if (server_fails_requests.load()) {
    response.success = false;
  }
  return response;
}

TEST_CASE("A request the server failed is sent again with the SDK name and version") {
  test_utils::clearSDK();
  Countly &countly = Countly::getInstance();
  countly.setHTTPClient(sendHTTPToFlakyServer);
  countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly.SetPath(TEST_DATABASE_NAME);
  countly.setFailedRequestCooldown(1);
  // Only processRQDebug() sends: the update loop sleeps through the test, and immediate stop still ends it at once
  countly.enableImmediateRequestOnStop();
  countly.setUpdateInterval(60000);
  countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);
  REQUIRE(waitForRequestWith("method", "sc"));
  countly.processRQDebug();
  test_utils::http_call_queue.clear();

  countly.addEvent(cly::Event("click", 1));
  countly.flushEvents();
  server_fails_requests.store(true);
  countly.processRQDebug();
  server_fails_requests.store(false);
  // a failed request is tried again only after the cool-down
  countly.processRQDebug();
  CHECK(test_utils::http_call_queue.size() == 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  countly.processRQDebug();

  REQUIRE(test_utils::http_call_queue.size() == 2);
  test_utils::HTTPCall failed = test_utils::http_call_queue.at(0);
  test_utils::HTTPCall resent = test_utils::http_call_queue.at(1);
  CHECK(resent.data == failed.data);
  CHECK(resent.data.count("events") == 1);
  CHECK(resent.data["sdk_name"] == EXPECTED_SDK_NAME);
  CHECK(resent.data["sdk_version"] == COUNTLY_SDK_VERSION);
  CHECK(countly.checkRQSize() == 0);
}

#ifndef COUNTLY_USE_CUSTOM_HTTP
namespace {
#ifdef _WIN32
typedef SOCKET Socket;
typedef int SocketLength;
const Socket NO_SOCKET = INVALID_SOCKET;

void closeSocket(Socket socket) { ::closesocket(socket); }

/**
 * Waits for events on the sockets, as poll() does.
 */
int pollSockets(pollfd *sockets, size_t count, int milliseconds) { return ::WSAPoll(sockets, static_cast<ULONG>(count), milliseconds); }

/**
 * Reads what the peer sent, as recv() does.
 */
long receiveFrom(Socket socket, char *buffer, size_t size) { return ::recv(socket, buffer, static_cast<int>(size), 0); }
#else
typedef int Socket;
typedef socklen_t SocketLength;
const Socket NO_SOCKET = -1;

void closeSocket(Socket socket) { ::close(socket); }

/**
 * Waits for events on the sockets, as poll() does.
 */
int pollSockets(pollfd *sockets, size_t count, int milliseconds) { return ::poll(sockets, static_cast<nfds_t>(count), milliseconds); }

/**
 * Reads what the peer sent, as recv() does.
 */
long receiveFrom(Socket socket, char *buffer, size_t size) { return static_cast<long>(::recv(socket, buffer, size, 0)); }
#endif

/**
 * HTTP server on 127.0.0.1 for exercising the SDK's built-in HTTP client: libcurl, or WinHTTP on Windows.
 *
 * An answering server replies to every complete request with 200 and {"result":"Success"}
 * and records its request line and body; with a body rate, it reads each body at that many bytes per second, like a
 * slow connection. A silent server accepts connections and never replies; it records
 * how long each connection stayed open until the client closed it.
 */
class LocalHTTPServer {
public:
  explicit LocalHTTPServer(bool answering, size_t body_bytes_per_second = 0) : _answering(answering), _body_bytes_per_second(body_bytes_per_second) {
#ifdef _WIN32
    WSADATA winsock = {};
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
#endif
    _listener = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(_listener != NO_SOCKET);
    int reuse = 1;
    ::setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(_listener, 16) == 0);

    SocketLength length = sizeof(address);
    REQUIRE(::getsockname(_listener, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    _port = ntohs(address.sin_port);
    _thread = std::thread(&LocalHTTPServer::serve, this);
  }

  ~LocalHTTPServer() {
    stop();
#ifdef _WIN32
    ::WSACleanup();
#endif
  }

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
    closeSocket(_listener);
  }

  int port() const { return _port; }

  /**
   * Request lines received so far, such as "POST /i HTTP/1.1".
   */
  std::vector<std::string> requestLines() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _request_lines;
  }

  /**
   * Request bodies received so far, in the order of requestLines().
   */
  std::vector<std::string> requestBodies() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _request_bodies;
  }

  /**
   * For each connection the client closed, how long it had stayed open.
   */
  std::vector<std::chrono::milliseconds> closedConnectionDurations() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _closed_durations;
  }

  /**
   * The bytes of the request body that the server is reading now, 0 between requests.
   */
  size_t bodyBytesInProgress() const { return _body_bytes_in_progress.load(); }

private:
  struct HeldConnection {
    Socket descriptor;
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
      if (pollSockets(watched.data(), watched.size(), 50) <= 0) {
        continue;
      }

      std::vector<HeldConnection> still_open;
      for (size_t index = 0; index < held.size(); index++) {
        char buffer[4096];
        if (watched[index + 1].revents == 0 || receiveFrom(held[index].descriptor, buffer, sizeof(buffer)) > 0) {
          still_open.push_back(held[index]);
          continue;
        }
        closeSocket(held[index].descriptor);
        std::lock_guard<std::mutex> lock(_mutex);
        _closed_durations.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - held[index].accepted));
      }
      held.swap(still_open);

      if (watched[0].revents & POLLIN) {
        const Socket client = ::accept(_listener, nullptr, nullptr);
        if (client != NO_SOCKET && _answering) {
          answer(client);
        } else if (client != NO_SOCKET) {
          held.push_back({client, std::chrono::steady_clock::now()});
        }
      }
    }

    for (const HeldConnection &connection : held) {
      closeSocket(connection.descriptor);
    }
  }

  /**
   * Reads what the client sent, waiting at most five seconds for it. Returns what recv() returns, or -1 on a timeout.
   */
  static long receiveWithin5Seconds(Socket client, char *buffer, size_t size) {
    pollfd readable = {client, POLLIN, 0};
    if (pollSockets(&readable, 1, 5000) <= 0) {
      return -1;
    }
    return receiveFrom(client, buffer, size);
  }

  void answer(Socket client) {
    std::string request;
    char buffer[4096];
    std::string::size_type header_end;
    while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
      const long received = receiveWithin5Seconds(client, buffer, sizeof(buffer));
      if (received <= 0) {
        closeSocket(client);
        return;
      }
      request.append(buffer, static_cast<size_t>(received));
    }

    size_t content_length = 0;
    const std::string::size_type length_header = request.find("Content-Length: ");
    if (length_header != std::string::npos && length_header < header_end) {
      content_length = std::stoul(request.substr(length_header + 16));
    }
    // with a body rate, small reads paced against the clock give the client a steady slow connection
    const size_t read_size = _body_bytes_per_second > 0 ? std::max<size_t>(1, std::min(sizeof(buffer), _body_bytes_per_second / 20)) : sizeof(buffer);
    const std::chrono::steady_clock::time_point body_start = std::chrono::steady_clock::now();
    while (request.size() < header_end + 4 + content_length && !_stopping.load()) {
      const long received = receiveWithin5Seconds(client, buffer, read_size);
      if (received <= 0) {
        break;
      }
      request.append(buffer, static_cast<size_t>(received));
      const size_t body_bytes = request.size() - (header_end + 4);
      _body_bytes_in_progress.store(body_bytes);
      if (_body_bytes_per_second > 0) {
        std::this_thread::sleep_until(body_start + std::chrono::microseconds(body_bytes * 1000000 / _body_bytes_per_second));
      }
    }
    _body_bytes_in_progress.store(0);
    if (request.size() < header_end + 4 + content_length) {
      // the client gave up, or the server stopped, before the whole body arrived
      closeSocket(client);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _request_lines.push_back(request.substr(0, request.find("\r\n")));
      _request_bodies.push_back(request.substr(header_end + 4));
    }

    const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 20\r\nConnection: close\r\n\r\n{\"result\":\"Success\"}";
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#ifdef MSG_NOSIGNAL
    ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
#else
    ::send(client, response.data(), static_cast<int>(response.size()), 0);
#endif
    closeSocket(client);
  }

  const bool _answering;
  const size_t _body_bytes_per_second;
  std::atomic<size_t> _body_bytes_in_progress{0};
  Socket _listener = NO_SOCKET;
  int _port = 0;
  std::thread _thread;
  std::atomic<bool> _stopping{false};
  mutable std::mutex _mutex;
  std::vector<std::string> _request_lines;
  std::vector<std::string> _request_bodies;
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
      const std::vector<std::string> request_lines = server.requestLines();
      const std::vector<std::string> request_bodies = server.requestBodies();
      for (size_t index = 0; index < request_lines.size() && index < request_bodies.size(); index++) {
        if (request_lines[index].rfind("POST /i ", 0) == 0 && request_bodies[index].find("begin_session=1") != std::string::npos) {
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

  SUBCASE("every request reaches the server with the SDK name and version") {
    LocalHTTPServer server(true);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->enableRemoteConfig();
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", server.port(), false);
    countly->addEvent(cly::Event("click", 1));
    countly->flushEvents();

    std::vector<std::string> request_lines;
    std::vector<std::string> request_bodies;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      countly->processRQDebug();
      request_lines = server.requestLines();
      request_bodies = server.requestBodies();
      if (request_lines.size() >= 5 && countly->checkRQSize() == 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    countly.reset();
    server.stop();
    remove(database_path.c_str());

    std::multiset<std::string> kinds;
    REQUIRE(request_bodies.size() == request_lines.size());
    for (size_t index = 0; index < request_lines.size(); index++) {
      // Every request is a POST with its parameters in the body, and only the path in the request line: "POST /o/sdk HTTP/1.1"
      const std::string &request_line = request_lines[index];
      REQUIRE(request_line.rfind("POST /", 0) == 0);
      const std::string::size_type path_start = 5;
      const std::string::size_type path_end = request_line.rfind(' ');
      REQUIRE(path_end > path_start);
      CHECK(request_line.find('?') == std::string::npos);

      test_utils::HTTPCall call{true, request_line.substr(path_start, path_end - path_start), parseRequest(request_bodies[index])};
      kinds.insert(requestKind(call));
      CHECK(call.data["sdk_name"] == EXPECTED_SDK_NAME);
      CHECK(call.data["sdk_version"] == COUNTLY_SDK_VERSION);
    }
    CHECK(kinds == std::multiset<std::string>{"sc", "rc", "begin_session", "events", "hc"});
  }

  SUBCASE("a request to a server that never answers is given up after the request timeout") {
    LocalHTTPServer server(false);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", server.port(), false);

#ifdef _WIN32
    // WinHTTP waits for an answer as long as its receive timeout
    const std::chrono::seconds request_timeout(COUNTLY_HTTP_CONNECT_TIMEOUT_SECONDS);
#else
    const std::chrono::seconds request_timeout(COUNTLY_HTTP_REQUEST_TIMEOUT_SECONDS);
#endif
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

/**
 * Whether one of the request bodies a server received contains the text.
 */
static bool receivedRequestWith(const LocalHTTPServer &server, const std::string &text) {
  for (const std::string &request_body : server.requestBodies()) {
    if (request_body.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

TEST_CASE("Changing the server URL with the built-in HTTP client") {
  const std::string database_path = "test-countly-http.db";
  remove(database_path.c_str());
  LocalHTTPServer first_server(true);
  LocalHTTPServer second_server(true);
  std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
  countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly->SetPath(database_path);
  // Only processRQDebug() sends: the update loop sleeps through the test, and immediate stop still ends it at once
  countly->enableImmediateRequestOnStop();
  countly->setUpdateInterval(60000);
  countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", first_server.port(), false);
  countly->addEvent(cly::Event("recorded_before_the_change", 1));
  countly->flushEvents();

  countly->changeServerUrl("ftp://127.0.0.1", second_server.port());
  countly->changeServerUrl("127.0.0.1", second_server.port());
  countly->changeServerUrl("http://", second_server.port());
  const std::string url_before_the_change = countly->getConfiguration().serverUrl;
  const unsigned int port_before_the_change = countly->getConfiguration().port;
  countly->changeServerUrl("http://127.0.0.1///", second_server.port());
  const std::string changed_url = countly->getConfiguration().serverUrl;

  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    countly->processRQDebug();
    if (receivedRequestWith(second_server, "begin_session=1") && receivedRequestWith(second_server, "recorded_before_the_change") && receivedRequestWith(second_server, "method=sc") && countly->checkRQSize() == 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int queued_requests = countly->checkRQSize();

  countly.reset();
  first_server.stop();
  second_server.stop();
  remove(database_path.c_str());

  CHECK(url_before_the_change == "http://127.0.0.1");
  CHECK(port_before_the_change == static_cast<unsigned int>(first_server.port()));
  CHECK(changed_url == "http://127.0.0.1");
  CHECK(queued_requests == 0);
  // The requests queued before the change, and the SDK behavior settings fetch that follows it, reach the new server
  CHECK(receivedRequestWith(second_server, "begin_session=1"));
  CHECK(receivedRequestWith(second_server, "recorded_before_the_change"));
  CHECK(receivedRequestWith(second_server, "method=sc"));
  for (const std::string &request_line : second_server.requestLines()) {
    CHECK((request_line.rfind("POST /i ", 0) == 0 || request_line.rfind("POST /o/sdk ", 0) == 0));
  }
  // The first server got at most what the SDK sent before the change: the SDK behavior settings fetch and the health check
  for (const std::string &request_body : first_server.requestBodies()) {
    CHECK((request_body.find("method=sc") != std::string::npos || request_body.find("hc=") != std::string::npos));
  }
}
/**
 * Sends the queue until the server received a request with the text, for at most 10 seconds.
 */
static void sendUntilReceived(cly::Countly &countly, const LocalHTTPServer &server, const std::string &text) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline && !receivedRequestWith(server, text)) {
    countly.processRQDebug();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

TEST_CASE("Built-in HTTP client with a port or a path in the server URL") {
  const std::string database_path = "test-countly-http.db";
  remove(database_path.c_str());

  SUBCASE("the port in the URL is used") {
    LocalHTTPServer server(true);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->enableImmediateRequestOnStop();
    countly->setUpdateInterval(60000);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1:" + std::to_string(server.port()), 0, false);
    sendUntilReceived(*countly, server, "begin_session=1");

    countly.reset();
    server.stop();
    remove(database_path.c_str());
    CHECK(receivedRequestWith(server, "begin_session=1"));
  }

  SUBCASE("a path in the URL goes in front of every endpoint") {
    LocalHTTPServer server(true);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->enableImmediateRequestOnStop();
    countly->setUpdateInterval(60000);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1:" + std::to_string(server.port()) + "/countly/", 0, false);
    sendUntilReceived(*countly, server, "begin_session=1");
    sendUntilReceived(*countly, server, "method=sc");

    countly.reset();
    server.stop();
    remove(database_path.c_str());
    CHECK(receivedRequestWith(server, "begin_session=1"));
    CHECK(receivedRequestWith(server, "method=sc"));
    for (const std::string &request_line : server.requestLines()) {
      CHECK((request_line.rfind("POST /countly/i ", 0) == 0 || request_line.rfind("POST /countly/o/sdk ", 0) == 0));
    }
  }

  SUBCASE("changeServerUrl takes the port and the path from the URL") {
    LocalHTTPServer first_server(true);
    LocalHTTPServer second_server(true);
    std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
    countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
    countly->SetPath(database_path);
    countly->enableImmediateRequestOnStop();
    countly->setUpdateInterval(60000);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", first_server.port(), false);
    countly->changeServerUrl("http://127.0.0.1:" + std::to_string(second_server.port()) + "/analytics", 0);
    sendUntilReceived(*countly, second_server, "begin_session=1");

    countly.reset();
    first_server.stop();
    second_server.stop();
    remove(database_path.c_str());
    CHECK(receivedRequestWith(second_server, "begin_session=1"));
    for (const std::string &request_line : second_server.requestLines()) {
      CHECK((request_line.rfind("POST /analytics/i ", 0) == 0 || request_line.rfind("POST /analytics/o/sdk ", 0) == 0));
    }
  }
}

namespace {
/**
 * Shortens the time limits of the built-in HTTP client while it lives, and puts the defaults back after.
 */
class ShortHTTPTimeLimits {
public:
  ShortHTTPTimeLimits(unsigned int socket_seconds, unsigned int request_seconds) { cly::RequestModule::setTimeLimitsForTests(socket_seconds, request_seconds); }
  ~ShortHTTPTimeLimits() { cly::RequestModule::setTimeLimitsForTests(COUNTLY_HTTP_CONNECT_TIMEOUT_SECONDS, COUNTLY_HTTP_REQUEST_TIMEOUT_SECONDS); }
  ShortHTTPTimeLimits(const ShortHTTPTimeLimits &) = delete;
  ShortHTTPTimeLimits &operator=(const ShortHTTPTimeLimits &) = delete;
};

/**
 * Crash segmentation of 20 values of 10,000 characters, so that the crash request is about 200 KB and takes several
 * seconds on a slow connection. A stack trace can't make it that large: the SDK keeps 20,000 characters of it.
 */
std::map<std::string, std::string> largeCrashSegmentation() {
  std::map<std::string, std::string> segmentation;
  for (int part = 10; part < 30; part++) {
    segmentation["part_" + std::to_string(part)] = std::string(10000, 'x');
  }
  return segmentation;
}

/**
 * An instance that keeps crash segmentation values of up to 10,000 characters.
 */
std::shared_ptr<cly::Countly> instanceWithLargeValues(const std::string &database_path) {
  std::shared_ptr<cly::Countly> countly = std::make_shared<cly::Countly>();
  countly->setDeviceID(COUNTLY_TEST_DEVICE_ID);
  countly->SetPath(database_path);
  countly->setMaxValueSize(10000);
  return countly;
}
} // namespace

TEST_CASE("Built-in HTTP client on a slow connection") {
  const std::string database_path = "test-countly-slow-http.db";
  remove(database_path.c_str());
  // a request may take 1 second (libcurl), or 2 seconds to send and 2 to wait for the answer (WinHTTP), plus 1 second
  // for every 16 KB of its body
  ShortHTTPTimeLimits limits(2, 1);

  SUBCASE("a large request arrives when it takes longer than the request timeout, while the connection keeps moving") {
    LocalHTTPServer server(true, 32 * 1024);
    std::shared_ptr<cly::Countly> countly = instanceWithLargeValues(database_path);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", server.port(), false);
    countly->crash().recordException("large crash", "stack trace", true, {}, largeCrashSegmentation());

    // at 32 KB per second, the crash request takes about 6 seconds
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!receivedRequestWith(server, "crash=") && std::chrono::steady_clock::now() < deadline) {
      countly->processRQDebug();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const bool crash_arrived = receivedRequestWith(server, "crash=");
    countly.reset();
    server.stop();
    remove(database_path.c_str());

    CHECK(crash_arrived);
  }

  SUBCASE("stop() waits no longer than the request timeout for a large request in progress") {
    LocalHTTPServer server(true, 8 * 1024);
    std::shared_ptr<cly::Countly> countly = instanceWithLargeValues(database_path);
    countly->setUpdateInterval(100);
    countly->start(COUNTLY_TEST_APP_KEY, "http://127.0.0.1", server.port(), true);
    countly->crash().recordException("large crash", "stack trace", true, {}, largeCrashSegmentation());

    // the update loop sends the crash request, whose body the server reads in about 25 seconds
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (server.bodyBytesInProgress() < 16 * 1024 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool sending = server.bodyBytesInProgress() >= 16 * 1024;
    const std::chrono::steady_clock::time_point stop_start = std::chrono::steady_clock::now();
    countly->stop();
    const std::chrono::steady_clock::duration stop_duration = std::chrono::steady_clock::now() - stop_start;
    countly.reset();
    server.stop();
    remove(database_path.c_str());

    REQUIRE(sending);
    CHECK(stop_duration < std::chrono::seconds(4));
  }
}

#endif
