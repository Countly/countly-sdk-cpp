#include <chrono>
#include <string>
#include <sstream>
#include <thread>
#include <iomanip>
#include <iostream>

#include "countly.hpp"

#ifndef COUNTLY_USE_CUSTOM_HTTP
#ifdef _WIN32
#include "Windows.h"
#include "WinHTTP.h"
#else
#include "curl/curl.h"
#endif
#endif

#ifdef COUNTLY_USE_SQLITE
#include "sqlite3.h"
#endif

Countly::Countly() : max_events(200), running(false) {
#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
	curl_global_init(CURL_GLOBAL_ALL);
#endif
}

Countly::~Countly() {
	this->stop();
#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
	curl_global_cleanup();
#endif
}

Countly& Countly::getInstance() {
	static Countly instance;
	return instance;
}

void Countly::setLogger(void (*fun)(Countly::LogLevel level, const std::string& message)) {
	mutex.lock();
	logger_function = fun;
	mutex.unlock();
}

void Countly::setHTTPClient(bool (*fun)(bool use_post, const std::string& url, const std::string& data)) {
	mutex.lock();
	http_client_function = fun;
	mutex.unlock();
}

void Countly::setMetrics(const std::string& os, const std::string& os_version, const std::string& device,
			 const std::string& resolution, const std::string& carrier, const std::string& app_version) {
	std::ostringstream json_buffer;

	json_buffer << '{';

	if (!os.empty()) {
		json_buffer << "_os:" << Countly::formatJSONString(os) << ',';
	}

	if (!os_version.empty()) {
		json_buffer << "_os_version:" << Countly::formatJSONString(os_version) << ',';
	}

	if (!device.empty()) {
		json_buffer << "_device:" << Countly::formatJSONString(device) << ',';
	}

	if (!resolution.empty()) {
		json_buffer << "_resolution:" << Countly::formatJSONString(resolution) << ',';
	}

	if (!carrier.empty()) {
		json_buffer << "_carrier:" << Countly::formatJSONString(carrier) << ',';
	}

	if (!app_version.empty()) {
		json_buffer << "_app_version:" << Countly::formatJSONString(app_version) << ',';
	}

	if (json_buffer.str() != "{") {
		json_buffer.seekp(-1, json_buffer.cur);
	}

	json_buffer << '}';
	mutex.lock();
	metrics = json_buffer.str();
	mutex.unlock();
}

void Countly::start(const std::string& app_key, const std::string& device_id, const std::string& host, int port, bool start_thread) {
	mutex.lock();
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
	this->device_id = device_id;

	if (port <= 0) {
		this->port = use_https ? 443 : 80;
	} else {
		this->port = port;
	}

	if (!running && start_thread) {
		stop_thread = false;
		thread = new std::thread(&Countly::updateLoop, this);
	}
	mutex.unlock();
}

void Countly::startOnCloud(const std::string& app_key, const std::string& device_id) {
	this->start(app_key, device_id, "https://cloud.count.ly", 443);
}

void Countly::stop() {
	mutex.lock();
	if (began_session) {
		mutex.unlock();
		endSession();
		mutex.lock();
	}
	stop_thread = true;
	mutex.unlock();
	if (thread != nullptr && thread->joinable()) {
		thread->join();
		delete thread;
	}
}

void Countly::addEvent(const Event& event) {
	mutex.lock();
#ifndef COUNTLY_USE_SQLITE
	if (event_queue.size() == max_events) {
		log(Countly::LogLevel::WARNING, "Event queue is full, dropping the oldest event to insert a new one");
		event_queue.pop_front();
	}
	event_queue.push_back(event);
#else
	sqlite3 *database;
	int return_value;
	char *error_message;

	return_value = sqlite3_open(database_path.c_str(), &database);
	if (return_value == SQLITE_OK) {
		std::ostringstream sql_statement_stream;
		sql_statement_stream << "INSERT INTO events (event) VALUES('" << event.serialize() << "');";
		std::string sql_statement = sql_statement_stream.str();

		return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
		if (return_value != SQLITE_OK) {
			log(Countly::LogLevel::ERROR, error_message);
			sqlite3_free(error_message);
		}
	}
	sqlite3_close(database);
#endif
	mutex.unlock();
}

bool Countly::beginSession() {
	mutex.lock();
	std::map<std::string, std::string> data = {{"app_key", app_key}, {"device_id", device_id}, {"sdk_version", COUNTLY_API_VERSION}, {"begin_session", "1"}, {"metrics", metrics}};
	if (sendHTTP("/i", Countly::serializeForm(data))) {
		last_sent = Countly::getTimestamp();
		began_session = true;
	}

	mutex.unlock();
	return began_session;
}

bool Countly::updateSession() {
	mutex.lock();
	if (!began_session) {
		mutex.unlock();
		if (!beginSession()) {
			return false;
		}

		mutex.lock();
		began_session = true;
	}

	std::ostringstream json_buffer;
	bool no_events;

#ifndef COUNTLY_USE_SQLITE
	no_events = event_queue.empty();
	if (!no_events) {
		json_buffer << '[';

		for (const auto& event: event_queue) {
			json_buffer << event.serialize() << ',';
		}

		json_buffer.seekp(-1, json_buffer.cur);
		json_buffer << ']';
	}
#else
	sqlite3 *database;
	int return_value, row_count, column_count;
	char** table;
	char *error_message;
	std::string event_ids;

	return_value = sqlite3_open(database_path.c_str(), &database);
	if (return_value == SQLITE_OK) {
		std::ostringstream sql_statement_stream;
		sql_statement_stream << "SELECT evtid, event FROM events LIMIT " << std::dec << max_events << ';';
		std::string sql_statement = sql_statement_stream.str();

		return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
		no_events = (row_count == 0);
		if (return_value == SQLITE_OK && !no_events) {
			std::ostringstream event_id_stream;

			json_buffer << '[';
			event_id_stream << '(';

			for (int event_index = 1; event_index < row_count+1; event_index++) {
				event_id_stream << table[event_index * column_count] << ',';
				json_buffer << table[(event_index * column_count) + 1] << ',';
			}

			event_id_stream.seekp(-1, json_buffer.cur);
			event_id_stream << ')';
			event_ids = event_id_stream.str();

			json_buffer.seekp(-1, json_buffer.cur);
			json_buffer << ']';
		} else if (return_value != SQLITE_OK) {
			log(Countly::LogLevel::ERROR, error_message);
			sqlite3_free(error_message);
		}
		sqlite3_free_table(table);
	}
	sqlite3_close(database);
#endif

	mutex.unlock();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
	mutex.lock();
	if (no_events) {
		if (duration.count() > COUNTLY_KEEPALIVE_INTERVAL) {
			std::map<std::string, std::string> data = {{"app_key", app_key}, {"device_id", device_id}, {"session_duration", std::to_string(duration.count())}};
			if (!sendHTTP("/i", Countly::serializeForm(data))) {
				mutex.unlock();
				return false;
			}
			last_sent += duration;
		}
		return true;
	} else {
		std::map<std::string, std::string> data = {{"app_key", app_key}, {"device_id", device_id}, {"session_duration", std::to_string(duration.count())}, {"events", json_buffer.str()}};
		if (!sendHTTP("/i", Countly::serializeForm(data))) {
			mutex.unlock();
			return false;
		}
		last_sent = Countly::getTimestamp();
	}

	if (!no_events) {
#ifndef COUNTLY_USE_SQLITE
		event_queue.clear();
#else
		return_value = sqlite3_open(database_path.c_str(), &database);
		if (return_value == SQLITE_OK) {
			std::ostringstream sql_statement_stream;
			sql_statement_stream << "DELETE FROM events WHERE evtid IN " << event_ids << ';';
			std::string sql_statement = sql_statement_stream.str();

			return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
			if (return_value != SQLITE_OK) {
				log(Countly::LogLevel::ERROR, error_message);
				sqlite3_free(error_message);
			}
		}
		sqlite3_close(database);
#endif
	}

	mutex.unlock();
	return true;
}

bool Countly::endSession() {
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration());
	mutex.lock();
	std::map<std::string, std::string> data = {{"app_key", app_key}, {"device_id", device_id}, {"session_duration", std::to_string(duration.count())}, {"end_session", "1"}};
	if (sendHTTP("/i", Countly::serializeForm(data))) {
		last_sent += duration;
		began_session = false;
		mutex.unlock();
		return true;
	}

	mutex.unlock();
	return false;
}

std::chrono::system_clock::time_point Countly::getTimestamp() {
	return std::chrono::system_clock::now();
}

std::string Countly::encodeURL(const std::string& data) {
	std::ostringstream encoded;

	for (auto character: data) {
		if (std::isalnum(character) || character == '.' || character == '_' || character == '~') {
			encoded << character;
		} else {
			encoded << '%' << std::setw(2) << std::hex << std::uppercase << (int)((unsigned char) character);
		}
	}

	return encoded.str();
}

std::string Countly::serializeForm(const std::map<std::string, std::string> data) {
	std::ostringstream serialized;

	for (const auto& key_value: data) {
		serialized << key_value.first << "=" << Countly::encodeURL(key_value.second) << '&';
	}

	std::string serialized_string = serialized.str();
	serialized_string.resize(serialized_string.size() - 1);

	return serialized_string;
}

std::string Countly::formatJSONString(const std::string& string) {
	std::string formattedString = string;

	for (auto index = formattedString.find('"', 0);
	     index != std::string::npos;
	     index = formattedString.find('"', index + 1)) {
		if (index == 0 || formattedString[index - 1] == '\\') {
			formattedString.insert(index, 1, '\\');
		}
	}

	formattedString.insert(0, 1, '\"');
	formattedString.push_back('\"');
	return formattedString;
}

#ifdef COUNTLY_USE_SQLITE
void Countly::setDatabasePath(const std::string& path) {
	sqlite3 *database;
	int return_value, row_count, column_count;
	char** table;
	char *error_message;

	mutex.lock();
	database_path = path;

	return_value = sqlite3_open(database_path.c_str(), &database);
	if (return_value == SQLITE_OK) {
		return_value = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS events (evtid INTEGER PRIMARY KEY, event TEXT)", nullptr, nullptr, &error_message);
		if (return_value != SQLITE_OK) {
			log(Countly::LogLevel::ERROR, error_message);
			sqlite3_free(error_message);
		}
	}
	sqlite3_close(database);
	mutex.unlock();
}
#endif

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
		full_url += ':';
		full_url += std::to_string(port);
		full_url += path;

		if (data.size() <= COUNTLY_POST_THRESHOLD) {
			full_url += '?';
			full_url += data;
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

std::chrono::system_clock::duration Countly::getSessionDuration() {
	mutex.lock();
	std::chrono::system_clock::duration duration = last_sent - Countly::getTimestamp();
	mutex.unlock();
	return duration;
}

void Countly::updateLoop() {
	mutex.lock();
	running = true;
	mutex.unlock();
	while (true) {
		mutex.lock();
		if (stop_thread) {
			stop_thread = false;
			mutex.unlock();
			break;
		}
		mutex.unlock();
		updateSession();
		std::this_thread::sleep_for(std::chrono::milliseconds(COUNTLY_KEEPALIVE_INTERVAL));
	}
	mutex.lock();
	running = false;
	mutex.unlock();
}
