#include <chrono>
#include <string>
#include <sstream>
#include <thread>
#include <iomanip>
#include <iostream>

#include "openssl/sha.h"

#include "countly.hpp"

#include "nlohmann/json.hpp"
using json = nlohmann::json;

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

Countly::Countly() : max_events(200)  {
#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
	curl_global_init(CURL_GLOBAL_ALL);
#endif
}

Countly::~Countly() {
	stop();
#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
	curl_global_cleanup();
#endif
}

Countly& Countly::getInstance() {
	static Countly instance;
	return instance;
}

void Countly::alwaysUsePost(bool value) {
	mutex.lock();
	always_use_post = value;
	mutex.unlock();
}

void Countly::setSalt(const std::string& value) {
	mutex.lock();
	salt = value;
	mutex.unlock();
}

void Countly::setLogger(void (*fun)(Countly::LogLevel level, const std::string& message)) {
	mutex.lock();
	logger_function = fun;
	mutex.unlock();
}

void Countly::setHTTPClient(Countly::HTTPResponse (*fun)(bool use_post, const std::string& url, const std::string& data)) {
	mutex.lock();
	http_client_function = fun;
	mutex.unlock();
}

void Countly::setMetrics(const std::string& os, const std::string& os_version, const std::string& device,
			 const std::string& resolution, const std::string& carrier, const std::string& app_version) {
	json metrics_object = json::object();

	if (!os.empty()) {
		metrics_object["os"] = os;
	}

	if (!os_version.empty()) {
		metrics_object["os_version"] = os_version;
	}

	if (!device.empty()) {
		metrics_object["device"] = device;
	}

	if (!resolution.empty()) {
		metrics_object["resolution"] = resolution;
	}

	if (!carrier.empty()) {
		metrics_object["carrier"] = carrier;
	}

	if (!app_version.empty()) {
		metrics_object["app_version"] = app_version;
	}

	mutex.lock();
	metrics = metrics_object.dump();
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
	event_queue.push_back(event.serialize());
#else
	sqlite3 *database;
	int return_value;
	char *error_message;

	return_value = sqlite3_open(database_path.c_str(), &database);
	if (return_value == SQLITE_OK) {
		std::ostringstream sql_statement_stream;
		// TODO Investigate if we need to escape single quotes in serialized event
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
	std::map<std::string, std::string> data = {
		{"app_key", app_key},
		{"device_id", device_id},
		{"sdk_version", COUNTLY_API_VERSION},
		{"metrics", metrics},
		{"sdk_name", COUNTLY_SDK_NAME},
		{"sdk_version", COUNTLY_SDK_VERSION},
		{"begin_session", "1"}
	};
	if (sendHTTP("/i", Countly::serializeForm(data)).success) {
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

		for (const auto& event_json: event_queue) {
			json_buffer << event_json << ',';
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
			if (!sendHTTP("/i", Countly::serializeForm(data)).success) {
				mutex.unlock();
				return false;
			}
			last_sent += duration;
		}
		return true;
	} else {
		std::map<std::string, std::string> data = {{"app_key", app_key}, {"device_id", device_id}, {"session_duration", std::to_string(duration.count())}, {"events", json_buffer.str()}};
		if (!sendHTTP("/i", Countly::serializeForm(data)).success) {
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
	if (sendHTTP("/i", Countly::serializeForm(data)).success) {
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

static size_t countly_curl_write_callback(void *data, size_t byte_size, size_t n_bytes, std::string *body) {
	size_t data_size = byte_size * n_bytes;
	body->append((const char *) data, data_size);
	return data_size;
}

Countly::HTTPResponse Countly::sendHTTP(std::string path, std::string data) {
	bool use_post = always_use_post || (data.size() > COUNTLY_POST_THRESHOLD);

	if (!salt.empty()) {
		unsigned char checksum[SHA256_DIGEST_LENGTH];
		std::string salted_data = data + salt;
		SHA256_CTX sha256;

		SHA256_Init(&sha256);
		SHA256_Update(&sha256, salted_data.c_str(), salted_data.size());
		SHA256_Final(checksum, &sha256);

		std::ostringstream checksum_stream;
		for (size_t index = 0; index < SHA256_DIGEST_LENGTH; index++) {
			checksum_stream << std::setw(2) << std::hex << checksum[index];
		}

		if (!data.empty()) {
			data += '&';
		}

		data += "checksum256=";
		data += checksum_stream.str();
	}
#ifdef COUNTLY_USE_CUSTOM_HTTP
	if (http_client_function == nullptr) {
		log(Countly::LogLevel::FATAL, "Missing HTTP client function");
		return false;
	}

	return http_client_function(use_post, path, data);
#else
	if (http_client_function != nullptr) {
		return http_client_function(use_post, path, data);
	}

	Countly::HTTPResponse response;
	response.success = false;
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
		if (!use_post) {
			path += '?';
			path += data;
		}

		size_t buffer_size = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
		wchar_t *wide_path= new wchar_t[buffer_size];
		MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wide_path, buffer_size);

		hRequest = WinHttpOpenRequest(hConnect, use_post ? L"POST" : L"GET", wide_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, use_https ? WINHTTP_FLAG_SECURE : 0);
	}

	if (hRequest) {
		bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, use_post ? data.c_str() : WINHTTP_NO_REQUEST_DATA, use_post ? data.size() : 0, 0, nullptr) != 0;
		if (ok) {
			ok = !WinHttpReceiveResponse(hRequest, NULL);
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

						char *body_part = new char[n_bytes_available];
						DWORD n_bytes_read = 0;

						if (!WinHttpReadData(hRequest, body_part, n_bytes_available, &n_bytes_read)) {
							error_reading_body = true;
							break;
						}

						body += body_part;
					} while (n_bytes_available > 0);

					if (!body.empty()) {
						response.data = json::parse(body);
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
	CURL* curl;
	CURLcode curl_code;
	curl = curl_easy_init();
	if (curl) {
		std::ostringstream full_url_stream;
		full_url_stream << host << ':' << std::dec << port << path;

		if (!use_post) {
			full_url_stream << '?' << data;
			curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
		} else {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
		}

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
				response.data = json::parse(body);
			}
		}

		curl_easy_cleanup(curl);
	}
#endif
	return response;
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
