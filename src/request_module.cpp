#include "countly/request_module.hpp"
#include "countly/health_check_module.hpp"
#include "countly/request_builder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <iomanip>

#ifndef COUNTLY_USE_CUSTOM_SHA256
#include "openssl/sha.h"
#endif

#ifndef COUNTLY_USE_CUSTOM_HTTP
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows.h"
#include "WinHTTP.h"
#undef ERROR
#pragma comment(lib, "winhttp.lib")
#else
#include "curl/curl.h"
#endif
#endif

#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
#define COUNTLY_HAS_CURL 1
#else
#define COUNTLY_HAS_CURL 0
#endif

namespace cly {
class RequestModule::RequestModuleImpl {
private:
public:
  bool is_queue_being_processed = false;
  // Guards the server URL and the port in the configuration, which a server URL change replaces while requests are sent.
  std::mutex server_mutex;
  // The id of the request processQueue() is sending, -1 while it sends none, and whether a queue operation removed
  // that request meanwhile. Guarded by the mutex processQueue() takes.
  long long sending_id = -1;
  bool sending_removed = false;
  // Both guarded by the instance mutex: no queued request is sent before them.
  std::chrono::steady_clock::time_point next_attempt_after_failure;
  std::chrono::steady_clock::time_point backoff_ends;
  std::function<void()> on_merge_delivered;
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestBuilder> _requestBuilder;
  std::shared_ptr<StorageModuleBase> _storageModule;
  RequestModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule)
      : _configuration(config), _logger(logger), _requestBuilder(requestBuilder), _storageModule(storageModule) {
    setServerUrl(_configuration->serverUrl, _configuration->port);
  }

  /**
   * Normalizes and stores the server URL and the port, see RequestModule::setServerUrl().
   */
  void setServerUrl(std::string url, unsigned int port) {
    const bool use_https = url.find("https://") == 0;
    if (!use_https && url.find("http://") != 0) {
      url.insert(0, "http://");
    }

    const size_t scheme_length = use_https ? (sizeof("https://") - 1) : (sizeof("http://") - 1);
    while (url.size() > scheme_length && url.back() == '/') {
      url.pop_back();
    }

    if (port == 0) {
      port = use_https ? 443 : 80;
    }

    std::lock_guard<std::mutex> lk(server_mutex);
    _configuration->serverUrl = url;
    _configuration->port = port;
  }

  ~RequestModuleImpl() { _logger.reset(); }

  std::string calculateChecksum(const std::string &salt, const std::string &data) {
    std::string salted_data = data + salt;
#ifdef COUNTLY_USE_CUSTOM_SHA256
    if (_configuration->sha256_function == nullptr) {
      _logger->log(LogLevel::FATAL, "[Countly] [RequestModule] calculateChecksum, Missing SHA 256 function");
      return {};
    }

    return _configuration->sha256_function(salted_data);
#else
    unsigned char checksum[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, salted_data.c_str(), salted_data.size());
    SHA256_Final(checksum, &sha256);

    std::ostringstream checksum_stream;
    for (size_t index = 0; index < SHA256_DIGEST_LENGTH; index++) {
      checksum_stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(checksum[index]);
    }

    return checksum_stream.str();
#endif
  }
};

namespace {
/**
 * The "timestamp" parameter of a stored request in milliseconds, or -1 when the request has none.
 */
long long requestTimestamp(const std::string &request) {
  std::string::size_type position = 0;
  if (request.compare(0, 10, "timestamp=") != 0) {
    position = request.find("&timestamp=");
    if (position == std::string::npos) {
      return -1;
    }
    position += 1;
  }
  position += 10;
  const std::string::size_type end = request.find('&', position);
  const std::string value = request.substr(position, end == std::string::npos ? std::string::npos : end - position);
  if (value.empty() || value.size() > 18 || value.find_first_not_of("0123456789") != std::string::npos) {
    return -1;
  }
  return std::stoll(value);
}

/**
 * The current time in milliseconds since the epoch, the unit of the "timestamp" parameter.
 */
long long nowMilliseconds() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

/**
 * Whether the request is older than the drop age. A request without a timestamp is never too old.
 */
bool isOlderThan(const std::string &request, unsigned int dropAgeHours) {
  if (dropAgeHours == 0) {
    return false;
  }
  const long long timestamp = requestTimestamp(request);
  return timestamp >= 0 && nowMilliseconds() - timestamp > static_cast<long long>(dropAgeHours) * 3600LL * 1000LL;
}

/**
 * Whether the answer to a queued request means it was delivered: a 2xx status and a JSON object with a "result".
 */
bool isDelivered(const HTTPResponse &response) { return response.success && response.data.is_object() && response.data.contains("result"); }

/**
 * Process-wide libcurl initialisation.
 *
 * libcurl's global init/cleanup pair is process-wide, and curl_global_cleanup()
 * must not run while another thread is using curl. Calling it when one SDK
 * instance is destroyed would tear networking down under every other live
 * instance, so cleanup happens exactly once: either at process exit (this is a
 * function-local static) or when the integrator calls
 * Countly::shutdownNetworking().
 *
 * The counters are maintained even on builds without curl so the tests mean the
 * same thing on every platform.
 */
class CurlGlobal {
public:
  CurlGlobal() {
#if COUNTLY_HAS_CURL
    curl_global_init(CURL_GLOBAL_ALL);
#endif
    _init_count.fetch_add(1);
  }

  ~CurlGlobal() { release(); }

  void release() {
    if (_released.exchange(true)) {
      return;
    }
#if COUNTLY_HAS_CURL
    curl_global_cleanup();
#endif
  }

  int initCount() const { return _init_count.load(); }
  bool released() const { return _released.load(); }

private:
  std::atomic<int> _init_count{0};
  std::atomic<bool> _released{false};
};

CurlGlobal &curlGlobal() {
  static CurlGlobal instance;
  return instance;
}

/**
 * How many times initGlobalNetworking() was asked to initialise, as opposed to
 * how many times curl actually was. The gap between the two is the whole point
 * of CurlGlobal, so both numbers are needed to assert that deduplication works
 * -- the actual count is 1 by construction and proves nothing on its own.
 */
std::atomic<int> networking_init_requests(0);

/**
 * Finds the app_key value of a stored request: sets 'start' and 'length' and returns true, or returns false when the
 * request carries none. Stored values are URL-encoded, so a "&" always starts the next parameter.
 */
bool findAppKey(const std::string &request, std::string::size_type &start, std::string::size_type &length) {
  start = request.compare(0, 8, "app_key=") == 0 ? 0 : request.find("&app_key=");
  if (start == std::string::npos) {
    return false;
  }
  start = request.find('=', start) + 1;
  const std::string::size_type end = request.find('&', start);
  length = (end == std::string::npos ? request.size() : end) - start;
  return true;
}
} // namespace

void RequestModule::initGlobalNetworking() {
  networking_init_requests.fetch_add(1);
  curlGlobal();
}

void RequestModule::releaseGlobalNetworking() { curlGlobal().release(); }

#ifdef COUNTLY_BUILD_TESTS
int RequestModule::globalNetworkingInitCount() { return curlGlobal().initCount(); }

int RequestModule::globalNetworkingInitRequests() { return networking_init_requests.load(); }

bool RequestModule::globalNetworkingReleased() { return curlGlobal().released(); }
#endif

RequestModule::RequestModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder, std::shared_ptr<StorageModuleBase> storageModule) {
  impl.reset(new RequestModuleImpl(config, logger, requestBuilder, storageModule));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [RequestModule] Initialized"));

  initGlobalNetworking();
}

RequestModule::~RequestModule() { impl.reset(); }

static size_t countly_curl_write_callback(void *data, size_t byte_size, size_t n_bytes, std::string *body) {
  size_t data_size = byte_size * n_bytes;
  body->append((const char *)data, data_size);
  return data_size;
}

void RequestModule::addRequestToQueue(const std::map<std::string, std::string> &data) {
  std::shared_ptr<ConfigurationProvider> config = _configProvider.lock();
  if (!config) {
    impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] addRequestToQueue: ConfigurationProvider unavailable. Not adding request.");
    return;
  }

  if (config->isTrackingEnabled() == false) {
    impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] addRequestToQueue: The SDK behavior settings turned tracking off, so the request is not recorded.");
    impl->_logger->recordLogCode("w505");
    return;
  }

  const long long limit = config->getRequestQueueSizeLimit();
  long long count = impl->_storageModule->RQCount();
  if (count >= limit) {
    const unsigned int dropAgeHours = config->getRequestDropAgeHours();
    while (count > 0) {
      std::shared_ptr<DataEntry> front = impl->_storageModule->RQPeekFront();
      if (front->getId() < 0 || !isOlderThan(front->getData(), dropAgeHours)) {
        break;
      }
      impl->_storageModule->RQRemoveFront(front);
      impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] addRequestToQueue: A request older than the drop age of [" + std::to_string(dropAgeHours) + "] hours is removed without being sent.");
      impl->_logger->recordLogCode("w330");
      count--;
    }
  }
  if (count >= limit) {
    const long long overflow = count - limit;
    const long long toRemove = (impl->_configuration->gradualRequestCleanerEnabled ? std::min(overflow, 100LL) : overflow) + 1;
    impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] addRequestToQueue: Request Queue is full. Dropping the oldest [" + std::to_string(toRemove) + "] request(s).");
    for (long long index = 0; index < toRemove; index++) {
      impl->_storageModule->RQRemoveFront();
      impl->_logger->recordLogCode("w310");
    }
  }

  const std::string request = impl->_requestBuilder->buildRequest(data);
  impl->_storageModule->RQInsertAtEnd(request);
}

void RequestModule::clearRequestQueue() {
  impl->_storageModule->RQClearAll();
  impl->sending_removed = true;
}

void RequestModule::setServerUrl(const std::string &url, unsigned int port) { impl->setServerUrl(url, port); }

void RequestModule::replaceAppKeysInQueue(const std::string &appKey) {
  const std::string encodedKey = RequestBuilder::encodeURL(appKey);
  for (const std::shared_ptr<DataEntry> &request : impl->_storageModule->RQPeekAll()) {
    std::string data = request->getData();
    std::string::size_type start, length;
    if (findAppKey(data, start, length) && data.compare(start, length, encodedKey) != 0) {
      data.replace(start, length, encodedKey);
      impl->_storageModule->RQUpdate(request->getId(), data);
    }
  }
}

long long RequestModule::removeOtherAppKeysFromQueue(const std::string &appKey) {
  const std::string encodedKey = RequestBuilder::encodeURL(appKey);
  long long removed = 0;
  for (const std::shared_ptr<DataEntry> &request : impl->_storageModule->RQPeekAll()) {
    const std::string &data = request->getData();
    std::string::size_type start, length;
    if (findAppKey(data, start, length) && data.compare(start, length, encodedKey) == 0) {
      continue;
    }
    impl->_storageModule->RQRemove(request->getId());
    if (request->getId() == impl->sending_id) {
      impl->sending_removed = true;
    }
    removed++;
  }
  return removed;
}

void RequestModule::processQueue(std::shared_ptr<std::mutex> mutex) {
  std::shared_ptr<ConfigurationProvider> config;
  {
    // lock_guard so the mutex is released on every exit path of this block,
    // including early returns and any exception thrown while it is held.
    std::lock_guard<std::mutex> lk(*mutex);

    config = _configProvider.lock();
    if (!config) {
      impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] processQueue: ConfigurationProvider unavailable, skipping queue processing.");
      return;
    }
    if (config->isNetworkingEnabled() == false) {
      impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] processQueue: Networking is disabled. Not processing request queue.");
      return;
    }

    // making sure that no other thread is processing the queue
    if (impl->is_queue_being_processed) {
      return;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now < impl->next_attempt_after_failure) {
      impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] processQueue: Waiting for the cool-down after a failed request.");
      return;
    }
    if (now < impl->backoff_ends) {
      impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] processQueue: Waiting for the backoff to end.");
      return;
    }

    // if this is the only thread, mark that processing is happening
    impl->is_queue_being_processed = true;
  }

  // this counter is used to make sure that we don't get stuck in an infinite/long loop of request processing
  int processedRequestsCounter = 0;

  // From here on the processing flag is set, so it MUST be cleared on every exit
  // path. If an exception propagates out (e.g. from sendHTTP or a storage op),
  // reset the flag before rethrowing, otherwise the queue would be permanently
  // blocked (every later call would early-return at the is_queue_being_processed
  // guard above) for the rest of the SDK's lifetime.
  try {
    while (true) {
      std::shared_ptr<DataEntry> data;
      long long behind = 0;
      {
        std::lock_guard<std::mutex> lk(*mutex);
        impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [RequestModule] processQueue: Processing the request queue."));
        const long long count = impl->_storageModule->RQCount();
        if (count <= 0) {
          impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [RequestModule] processQueue: Queue is empty."));

          // stop sending requests once the queue is empty
          break;
        }

        data = impl->_storageModule->RQPeekFront();
        if (isOlderThan(data->getData(), config->getRequestDropAgeHours())) {
          impl->_storageModule->RQRemoveFront(data);
          impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] processQueue: A request older than the drop age of [" + std::to_string(config->getRequestDropAgeHours()) + "] hours is removed without being sent.");
          impl->_logger->recordLogCode("w330");
          continue;
        }
        impl->sending_id = data->getId();
        impl->sending_removed = false;
        behind = count - 1;
      }

      bool mergeDelivered = false;
      const std::chrono::steady_clock::time_point sent = std::chrono::steady_clock::now();
      HTTPResponse response = sendHTTP("/i", data->getData() + "&rr=" + std::to_string(behind));
      const std::chrono::steady_clock::duration responseTime = std::chrono::steady_clock::now() - sent;

      {
        std::lock_guard<std::mutex> lk(*mutex);
        impl->sending_id = -1;
        std::shared_ptr<HealthCheckModule> healthCheck = impl->_logger->healthCheck();
        if (!isDelivered(response)) {
          impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] processQueue: Failed to deliver to server, will try again after the cool-down of [" + std::to_string(impl->_configuration->failedRequestCooldownSeconds) + "] seconds.");
          if (healthCheck && response.status_code > 0) {
            healthCheck->logFailedNetworkRequest(response.status_code, !response.body.empty() || response.data.is_null() ? response.body : response.data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
          }
          impl->next_attempt_after_failure = std::chrono::steady_clock::now() + std::chrono::seconds(impl->_configuration->failedRequestCooldownSeconds);
          // if the request was not a success, abort sending and try again in the future
          break;
        }

        // we pop the front only if it is still the same request
        // the queue might have changed while we were sending the request
        // a removed request's id can belong to a newer request by now, so nothing is removed then
        if (!impl->sending_removed) {
          impl->_storageModule->RQRemoveFront(data);
        }
        processedRequestsCounter++;
        mergeDelivered = data->getData().find("old_device_id=") != std::string::npos;

        if (config->isBackoffEnabled()) {
          const long long timestamp = requestTimestamp(data->getData());
          const bool slow = responseTime >= std::chrono::seconds(config->getBackoffAcceptedTimeout());
          const bool queueNotFull = impl->_storageModule->RQCount() <= config->getBackoffRequestQueuePercentage() * config->getRequestQueueSizeLimit();
          const bool recent = timestamp >= 0 && nowMilliseconds() - timestamp <= static_cast<long long>(config->getBackoffRequestAge()) * 3600LL * 1000LL;
          if (slow && queueNotFull && recent) {
            impl->backoff_ends = std::chrono::steady_clock::now() + std::chrono::seconds(config->getBackoffDuration());
            impl->_logger->log(LogLevel::WARNING, "[Countly] [RequestModule] processQueue: The server answered slowly, so sending pauses for [" + std::to_string(config->getBackoffDuration()) + "] seconds.");
            if (healthCheck) {
              healthCheck->logBackoffRequest();
            }
            break;
          }
          if (healthCheck) {
            healthCheck->logConsecutiveBackoffRequest();
          }
        }

        if (processedRequestsCounter > impl->_configuration->maxProcessingBatchSize) {
          impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[Countly] [RequestModule] processQueue: Batch limit has been reached, will do next batch later."));
          break;
        }
      }
      if (mergeDelivered && impl->on_merge_delivered) {
        impl->on_merge_delivered();
      }
    }
  } catch (...) {
    std::lock_guard<std::mutex> lk(*mutex);
    // mark that no thread is processing the request queue, then let the
    // exception continue propagating to the update loop's handler.
    impl->is_queue_being_processed = false;
    throw;
  }

  std::lock_guard<std::mutex> lk(*mutex);
  // mark that no thread is processing the request queue
  impl->is_queue_being_processed = false;
}

HTTPResponse RequestModule::sendImmediate(const std::string &path, const std::string &data) { return sendHTTP(path, data + "&rr=" + std::to_string(std::max(0LL, RQSize()))); }

HTTPResponse RequestModule::sendHTTP(std::string path, std::string data) {
  const bool use_post = true;
  impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, data: [" + data + "]");
  if (!impl->_configuration->salt.empty()) {
    std::string checksum = impl->calculateChecksum(impl->_configuration->salt, data);
    if (!data.empty()) {
      data += '&';
    }

    data += "checksum256=" + checksum;
    impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, with checksum, data: [" + data + "]");
  }

  HTTPResponse response;
  response.success = false;

#ifdef COUNTLY_USE_CUSTOM_HTTP
  if (!impl->_configuration->http_client_function) {
    impl->_logger->log(LogLevel::FATAL, "[Countly] [RequestModule] sendHTTP, Missing HTTP client function");
    return response;
  }

  return impl->_configuration->http_client_function(use_post, path, data);
#else
  if (impl->_configuration->http_client_function) {
    return impl->_configuration->http_client_function(use_post, path, data);
  }

  std::string server_url;
  unsigned int port;
  {
    std::lock_guard<std::mutex> lk(impl->server_mutex);
    server_url = impl->_configuration->serverUrl;
    port = impl->_configuration->port;
  }
#ifdef _WIN32
  const bool use_https = server_url.find("https://") == 0;
  HINTERNET hSession = nullptr;
  HINTERNET hConnect = nullptr;
  HINTERNET hRequest = nullptr;

  hSession = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (hSession) {
    // Set way Shorter timeouts:
    WinHttpSetTimeouts(hSession,
                       10000,  // nResolveTimeout: 10sec (default 'infinite').
                       10000,  // nConnectTimeout: 10sec (default 60sec).
                       10000,  // nSendTimeout: 10sec (default 30sec).
                       10000); // nReceiveTimeout: 10sec (default 30sec).

    size_t scheme_offset = use_https ? (sizeof("https://") - 1) : (sizeof("http://") - 1);
    size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, server_url.c_str() + scheme_offset, -1, nullptr, 0);
    wchar_t *wide_hostname = new wchar_t[buffer_size];
    MultiByteToWideChar(CP_ACP, 0, server_url.c_str() + scheme_offset, -1, wide_hostname, static_cast<int>(buffer_size));

    hConnect = WinHttpConnect(hSession, wide_hostname, port, 0);

    delete[] wide_hostname;
  }

  if (hConnect) {
    size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    wchar_t *wide_path = new wchar_t[buffer_size];
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wide_path, static_cast<int>(buffer_size));

    hRequest = WinHttpOpenRequest(hConnect, L"POST", wide_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, use_https ? WINHTTP_FLAG_SECURE : 0);
    delete[] wide_path;
  }

  if (hRequest) {
    LPCWSTR headers = L"content-type:application/x-www-form-urlencoded";
    bool ok = WinHttpSendRequest(hRequest, headers, 0, (LPVOID)data.data(), static_cast<DWORD>(data.size()), static_cast<DWORD>(data.size()), 0) != 0;
    if (ok) {
      ok = WinHttpReceiveResponse(hRequest, NULL);
      if (ok) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = static_cast<DWORD>(sizeof(dwStatusCode));
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        response.success = (dwStatusCode >= 200 && dwStatusCode < 300);
        response.status_code = static_cast<long>(dwStatusCode);
        {
          DWORD n_bytes_available;
          bool error_reading_body = false;
          std::string body;
          do {
            n_bytes_available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &n_bytes_available)) {
              error_reading_body = true;
              break;
            }

            if (n_bytes_available == 0) {
              break;
            }

            char *body_part = new char[n_bytes_available + 1];
            memset(body_part, 0, n_bytes_available + 1);
            DWORD n_bytes_read = 0;

            if (!WinHttpReadData(hRequest, body_part, n_bytes_available, &n_bytes_read)) {
              error_reading_body = true;
              delete[] body_part;
              break;
            }

            body += body_part;
            delete[] body_part;
          } while (n_bytes_available > 0);

          response.body = body;
          if (response.success && !body.empty()) {
            const nlohmann::json &parseResult = nlohmann::json::parse(body, nullptr, false);
            if (parseResult.is_discarded()) {
              impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, Returned response from the server was not a valid JSON.");
            } else {
              response.data = parseResult;
            }
          }
        }
      }
    }

    WinHttpCloseHandle(hRequest);
  }

  if (hConnect) {
    WinHttpCloseHandle(hConnect);
  }

  if (hSession) {
    WinHttpCloseHandle(hSession);
  }
#else
  CURL *curl;
  CURLcode curl_code;
  curl = curl_easy_init();
  if (curl) {
    std::ostringstream full_url_stream;
    full_url_stream << server_url << ':' << std::dec << port << path;

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

    impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, request: [" + full_url_stream.str() + "]");

    std::string full_url = full_url_stream.str();
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    // Without these, a server that stops answering holds this request, and every request queued behind it, forever
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(COUNTLY_HTTP_CONNECT_TIMEOUT_SECONDS));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(COUNTLY_HTTP_REQUEST_TIMEOUT_SECONDS));

    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, countly_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    curl_code = curl_easy_perform(curl);
    if (curl_code == CURLE_OK) {

      long status_code;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
      response.success = (status_code >= 200 && status_code < 300);
      response.status_code = status_code;
      response.body = body;

      if (!body.empty()) {
        const nlohmann::json &parseResult = nlohmann::json::parse(body, nullptr, false);
        if (parseResult.is_discarded()) {
          impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, Returned response from the server was not a valid JSON.");
        } else {
          response.data = parseResult;
        }
      }
    }
    curl_easy_cleanup(curl);
  }
#endif
  impl->_logger->log(LogLevel::DEBUG, "[Countly] [RequestModule] sendHTTP, response: [" + response.data.dump() + "]");
  return response;
#endif
}
long long RequestModule::RQSize() { return impl->_storageModule->RQCount(); }

void RequestModule::setConfigurationProvider(std::weak_ptr<ConfigurationProvider> provider) { _configProvider = std::move(provider); }

void RequestModule::setOnMergeDelivered(std::function<void()> callback) { impl->on_merge_delivered = std::move(callback); }

} // namespace cly
