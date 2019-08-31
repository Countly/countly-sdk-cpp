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

#ifdef COUNTLY_USE_CUSTOM_HTTP
void Countly::setHTTPClient(bool (*fun)(bool is_post, const std::string& url, const std::string& data)) {
	http_client_function = fun;
}
#endif

void Countly::start(const std::string& app_key, const std::string& host, int port) {
	this->app_key = app_key;
	this->host = host;
	this->port = port;

	// TODO Start thread
}

void Countly::startOnCloud(const std::string& app_key) {
	this->start(app_key, "https://cloud.count.ly", 80);
}

void Countly::stop() {
	// TODO
}

void Countly::log(Countly::LogLevel level, const std::string& message) {
	if (logger_function != nullptr) {
		logger_function(level, message);
	}
}

bool Countly::sendHTTP(const std::string& url, const std::string& data) {
#ifdef COUNTLY_USE_CUSTOM_HTTP
	if (http_client_function == nullptr) {
		log(Countly::LogLevel::FATAL, "Missing HTTP client function");
		return false;
	}

	return http_client_function(data.size() > COUNTLY_POST_THRESHOLD, url, data);
#else
	bool ok = false;
#ifdef _WIN32
	HINTERNET hSession;
	HINTERNET hConnect;
	HINTERNET hRequest;

	hSession = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession) {
		wchar_t wideHostName[256];
		MultiByteToWideChar(0, 0, _appHostName.c_str(), -1, wideHostName, 256);
		hConnect = WinHttpConnect(hSession, wideHostName, _appPort, 0);
	}

	if (hConnect) {
		wchar_t wideURI[65536];
		MultiByteToWideChar(0, 0, URI.c_str(), -1, wideURI, 65536);
		hRequest = WinHttpOpenRequest(hConnect, L"GET", wideURI, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, _https ? WINHTTP_FLAG_SECURE : 0);
	}

	if (hRequest) {
		std::stringstream headers;
		headers << "User-Agent: Countly " << Countly::GetVersion();
		wchar_t wideHeaders[256];
		MultiByteToWideChar(0, 0, headers.str().c_str(), -1, wideHeaders, 256);
		ok = WinHttpSendRequest(hRequest, wideHeaders, headers.str().size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0;

		if (ok) {
			ok = WinHttpReceiveResponse(hRequest, NULL) != 0;
			if (ok) {
				DWORD dwStatusCode = 0;
				DWORD dwSize = sizeof(dwStatusCode);
				WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE |
						    WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
						    &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
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
		std::stringstream fullURI;
		fullURI << host << ':' << std::dec << port << url;

		if (data.size() <= COUNTLY_POST_THRESHOLD) {
			fullURI << '?' << data;
			curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
		} else {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
		}

		curl_easy_setopt(curl, CURLOPT_URL, fullURI.str().c_str());

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
