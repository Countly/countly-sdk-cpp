#include "countly/request_module.hpp"
#include "countly/request_builder.hpp"

#include <chrono>
#include <deque>
#include <iomanip>
#include <string>

#ifndef COUNTLY_USE_CUSTOM_SHA256
#include "openssl/sha.h"
#endif

#ifndef COUNTLY_USE_CUSTOM_HTTP
#ifdef _WIN32
#include "WinHTTP.h"
#include "Windows.h" //1: Order matters
#undef ERROR
#pragma comment(lib, "winhttp.lib")
#else
#include "curl/curl.h"
#endif
#include <mutex>
#endif

namespace cly {
class RequestModule::RequestModuleImpl {
private:
  bool use_https = true;

public:
  bool is_queue_being_processed = false;
  std::deque<std::string> request_queue;
  std::shared_ptr<CountlyConfiguration> _configuration;
  std::shared_ptr<LoggerModule> _logger;
  std::shared_ptr<RequestBuilder> _requestBuilder;
  RequestModuleImpl(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder) : _configuration(config), _logger(logger), _requestBuilder(requestBuilder) {
    if (_configuration->serverUrl.find("http://") == 0) {
      use_https = false;
    } else if (_configuration->serverUrl.find("https://") == 0) {
      use_https = true;
    }
  }

  ~RequestModuleImpl() { _logger.reset(); }

  std::string calculateChecksum(const std::string &salt, const std::string &data) {
    std::string salted_data = data + salt;
#ifdef COUNTLY_USE_CUSTOM_SHA256
    if (_configuration->sha256_function == nullptr) {
      _logger->log(LogLevel::FATAL, "Missing SHA 256 function");
      return {};
    }

    return sha256_function(salted_data);
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

RequestModule::RequestModule(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger, std::shared_ptr<RequestBuilder> requestBuilder) {
  impl.reset(new RequestModuleImpl(config, logger, requestBuilder));

  impl->_logger->log(LogLevel::DEBUG, cly::utils::format_string("[RequestModule] Initialized"));
}

RequestModule::~RequestModule() { impl.reset(); }

void RequestModule::addRequestToQueue(const std::map<std::string, std::string> &data) {
  if (impl->_configuration->requestQueueThreshold == impl->request_queue.size()) {
    impl->_logger->log(LogLevel::WARNING, cly::utils::format_string("[RequestModule] addRequestToQueue: Request Queue is full. Dropping the oldest request."));
    impl->request_queue.pop_front();
  }

  std::string &request = impl->_requestBuilder->buildRequest(data);
  impl->request_queue.push_back(request);
}

// void RequestModule::processQueue() {
//   // make it thread safe
//   while (!impl->request_queue.empty()) {
//     std::string data = impl->request_queue.front();
//     HTTPResponse response = impl->sendHTTP(data);
//
//     if (!response.success) {
//       break;
//     }
//
//     impl->request_queue.pop_back();
//   }
// }

void RequestModule::processQueue(std::shared_ptr<std::mutex> mutex) {
  mutex->lock();
  // making sure that no other thread is processing the queue
  if (impl->is_queue_being_processed) {
    mutex->unlock();
    return;
  }

  // if this is the only thread, mark that processing is happening
  impl->is_queue_being_processed = true;
  mutex->unlock();

  while (true) {
    mutex->lock();
    if (impl->request_queue.empty()) {
      // stop sending requests once the queue is empty
      mutex->unlock();
      break;
    }

    std::string data = impl->request_queue.front();
    mutex->unlock();

    HTTPResponse response = sendHTTP("/i", data);

    mutex->lock();
    if (!response.success) {
      // if the request was not a success, abort sending and try again in the future
      mutex->unlock();
      break;
    }

    if (impl->request_queue.front() == data) {
      // we pop the front only if it is still the same request
      // the queue might have changed while we were sending the request
      impl->request_queue.pop_front();
    }

    mutex->unlock();
  }

  mutex->lock();
  // mark that no thread is processing the request queue
  impl->is_queue_being_processed = false;
  mutex->unlock();
}

HTTPResponse RequestModule::sendHTTP(const std::string &path, std::string data) {
  bool use_post = impl->_configuration->forcePost || (data.size() > COUNTLY_POST_THRESHOLD);
  impl->_logger->log(LogLevel::DEBUG, "[Countly][sendHTTP] data: " + data);
  if (!impl->_configuration->salt.empty()) {
    std::string checksum = impl->calculateChecksum(impl->_configuration->salt, data);
    if (!data.empty()) {
      data += '&';
    }

    data += "checksum256=" + checksum;
    impl->_logger->log(LogLevel::DEBUG, "[Countly][sendHTTP] with checksum, data: " + data);
  }

  HTTPResponse response;
  response.success = false;

#ifdef COUNTLY_USE_CUSTOM_HTTP
  if (!impl->_configuration->http_client_function) {
    impl->_logger->log(LogLevel::FATAL, "Missing HTTP client function");
    return response;
  }

  return impl->_configuration->http_client_function(use_post, path, data);
#else
  if (impl->_configuration->http_client_function) {
    return impl->_configuration->http_client_function(use_post, path, data);
  }
#ifdef _WIN32
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
    size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, impl->_configuration->serverUrl.c_str() + scheme_offset, -1, nullptr, 0);
    wchar_t *wide_hostname = new wchar_t[buffer_size];
    MultiByteToWideChar(CP_ACP, 0, impl->_configuration->serverUrl.c_str() + scheme_offset, -1, wide_hostname, buffer_size);

    hConnect = WinHttpConnect(hSession, wide_hostname, impl->_configuration->port, 0);

    delete[] wide_hostname;
  }

  if (hConnect) {
    if (!use_post) {
      path += '?';
      path += data;
    }

    size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    wchar_t *wide_path = new wchar_t[buffer_size];
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wide_path, buffer_size);

    hRequest = WinHttpOpenRequest(hConnect, use_post ? L"POST" : L"GET", wide_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, use_https ? WINHTTP_FLAG_SECURE : 0);
    delete[] wide_path;
  }

  if (hRequest) {
    LPCWSTR headers = use_post ? L"content-type:application/x-www-form-urlencoded" : WINHTTP_NO_ADDITIONAL_HEADERS;
    bool ok = WinHttpSendRequest(hRequest, headers, 0, use_post ? (LPVOID)data.data() : WINHTTP_NO_REQUEST_DATA, use_post ? data.size() : 0, use_post ? data.size() : 0, 0) != 0;
    if (ok) {
      ok = WinHttpReceiveResponse(hRequest, NULL);
      if (ok) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        response.success = (dwStatusCode >= 200 && dwStatusCode < 300);
        if (response.success) {
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

          if (!body.empty()) {
            const nlohmann::json &parseResult = nlohmann::json::parse(body, nullptr, false);
            if (parseResult.is_discarded()) {
              impl->_logger->log(LogLevel::WARNING, "[Countly][sendHTTP] Returned response from the server was not a valid JSON.");
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
    full_url_stream << impl->_configuration->serverUrl << ':' << std::dec << impl->_configuration->port << path;

    if (!use_post) {
      full_url_stream << '?' << data;
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
    } else {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }

    impl->_logger->log(LogLevel::DEBUG, "[Countly][sendHTTP] request: " + full_url_stream.str());

    std::string full_url = full_url_stream.str();
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());

    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, countly_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    curl_code = curl_easy_perform(curl);
    if (curl_code == CURLE_OK) {

      long status_code;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
      response.success = (status_code >= 200 && status_code < 300);

      if (!body.empty()) {
        const nlohmann::json &parseResult = nlohmann::json::parse(body, nullptr, false);
        if (parseResult.is_discarded()) {
          impl->_logger->log(LogLevel::WARNING, "[Countly][sendHTTP] Returned response from the server was not a valid JSON.");
        } else {
          response.data = parseResult;
        }
      }
    }
    curl_easy_cleanup(curl);
  }
#endif
  impl->_logger->log(LogLevel::DEBUG, "[Countly][sendHTTP] response: " + response.data.dump());
  return response;
#endif
}
} // namespace cly