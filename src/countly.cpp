#include "countly.hpp"

#include <string>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#include <WinHTTP.h>
#else
#include <curl/curl.h>
#endif

Countly::Countly() {}

Countly::~Countly() {
	this->stop();
}

Countly& Countly::getInstance() {
	static Countly instance;
	return instance;
}

void Countly::setLogger(void (*fun)(Countly::LogLevel level, const std::string& message)) {
	logger_function = fun;
}

void Countly::setHTTPClient(bool (*fun)(bool use_post, const std::string& url, const std::string& data)) {
	http_client_function = fun;
}

void Countly::start(const std::string& app_key, const std::string& host, int port) {
	this->host = host;
	if (host.find("http://") == 0) {
		use_https = false;
	} else if (host.find("https://") == 0) {
		use_https = true;
	} else {
		use_https = false;
		this->host.insert(0, "http://");
	}

	this->app_key = app_key;

	if (port == -1) {
		this->port = use_https ? 443 : 80;
	} else {
		this->port = port;
	}

	// TODO Start thread
}

void Countly::startOnCloud(const std::string& app_key) {
	this->start(app_key, "https://cloud.count.ly", 443);
}

void Countly::stop() {
	// TODO
}

void Countly::log(Countly::LogLevel level, const std::string& message) {
	if (logger_function != nullptr) {
		logger_function(level, message);
	}
}

bool Countly::sendHTTP(const std::string& path, const std::string& data) {
	bool use_post = data.size() > COUNTLY_POST_THRESHOLD;
#ifdef COUNTLY_USE_CUSTOM_HTTP
	if (http_client_function == nullptr) {
		log(Countly::LogLevel::FATAL, "Missing HTTP client function");
		return false;
	}

	return http_client_function(data.size() > COUNTLY_POST_THRESHOLD, path, data);
#else
	if (http_client_function != nullptr) {
		return http_client_function(use_post, path, data);
	}

	bool ok = false;
#ifdef _WIN32
	HINTERNET hSession;
	HINTERNET hConnect;
	HINTERNET hRequest;

	hSession = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession) {
		size_t scheme_offset = use_https ? (sizeof("https://") - 1) : (sizeof("http://") - 1);
		size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, host.c_str() + scheme_offset, -1, nullptr, 0);
		wchar_t *wide_hostname = new wchar_t[buffer_size];
		MultiByteToWideChar(CP_ACP, 0, host.c_str() + scheme_offset, -1, wide_hostname, buffer_size);

		hConnect = WinHttpConnect(hSession, wide_hostname, port, 0);

		delete[] wide_hostname;
	}

	if (hConnect) {
		const std::string* full_path;

		if (use_post) {
			full_path = new std::string(path);
			*full_path += '?';
			*full_path += data;
		} else {
			full_path = &path;
		}

		size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, full_path.c_str(), -1, nullptr, 0);
		wchar_t *wide_path= new wchar_t[buffer_size];
		MultiByteToWideChar(CP_ACP, 0, full_path.c_str(), -1, wide_path, buffer_size);

		hRequest = WinHttpOpenRequest(hConnect, use_post ? L"POST" : L"GET", wide_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, use_https ? WINHTTP_FLAG_SECURE : 0);

		if (use_post) {
			delete full_path;
		}
	}

	if (hRequest) {
		ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, use_post ? data.c_str() : WINHTTP_NO_REQUEST_DATA, use_post ? data.size() : 0, 0, nullptr) != 0;
		if (ok) {
			ok = WinHttpReceiveResponse(hRequest, NULL) != 0;
			if (ok) {
				DWORD dwStatusCode = 0;
				DWORD dwSize = sizeof(dwStatusCode);
				WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
				ok = (dwStatusCode == 200);
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
	CURL* curl;
	CURLcode curl_code;
	curl = curl_easy_init();
	if (curl) {
		std::string full_url(host);
		host += ':';
		host += std::to_string(port);
		host += path;

		if (data.size() <= COUNTLY_POST_THRESHOLD) {
			host += '?';
			host += data;
			curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
		} else {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
		}

		curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());

		curl_code = curl_easy_perform(curl);
		if (curl_code == CURLE_OK) {
			long status_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
			ok = (status_code == 200);
		}

		curl_easy_cleanup(curl);
	}
#endif
	return ok;
#endif
}
