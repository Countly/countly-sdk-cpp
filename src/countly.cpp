#include <chrono>
#include <string>
#include <sstream>
#include <thread>
#include <iomanip>
#include <iostream>
#include <system_error>

#include "openssl/sha.h"

#include "countly.hpp"

#include "nlohmann/json.hpp"
using json = nlohmann::json;

#ifndef COUNTLY_USE_CUSTOM_HTTP
#ifdef _WIN32
#include "Windows.h"
#include "WinHTTP.h"
#undef ERROR
#pragma comment(lib, "winhttp.lib")
#else
#include "curl/curl.h"
#endif
#endif

#ifdef COUNTLY_USE_SQLITE
#include "sqlite3.h"
#endif

Countly::Countly() : max_events(COUNTLY_MAX_EVENTS_DEFAULT), wait_milliseconds(COUNTLY_KEEPALIVE_INTERVAL) {
	is_dispose = false;
#if !defined(_WIN32) && !defined(COUNTLY_USE_CUSTOM_HTTP)
	curl_global_init(CURL_GLOBAL_ALL);
#endif
}

Countly::~Countly() {
	is_dispose = true;
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
	json metrics = json::object();

	if (!os.empty()) {
		metrics["_os"] = os;
	}

	if (!os_version.empty()) {
		metrics["_os_version"] = os_version;
	}

	if (!device.empty()) {
		metrics["_device"] = device;
	}

	if (!resolution.empty()) {
		metrics["_resolution"] = resolution;
	}

	if (!carrier.empty()) {
		metrics["_carrier"] = carrier;
	}

	if (!app_version.empty()) {
		metrics["_app_version"] = app_version;
	}

	mutex.lock();
	session_params["metrics"] = metrics;
	mutex.unlock();
}

void Countly::setUserDetails(const std::map<std::string, std::string>& value) {
	mutex.lock();
	session_params["user_details"] = value;

	if (began_session) {
		std::map<std::string, std::string> data = {
			{"app_key", session_params["app_key"].dump()},
			{"device_id", session_params["device_id"].dump()},
			{"user_details", session_params["user_details"].dump()}
		};

		sendHTTP("/i", Countly::serializeForm(data));
	}
	mutex.unlock();
}

void Countly::setCustomUserDetails(const std::map<std::string, std::string>& value) {
	mutex.lock();
	session_params["user_details"]["custom"] = value;

	if (began_session) {
		std::map<std::string, std::string> data = {
			{"app_key", session_params["app_key"].dump()},
			{"device_id", session_params["device_id"].dump()},
			{"user_details", session_params["user_details"].dump()}
		};

		sendHTTP("/i", Countly::serializeForm(data));
	}
	mutex.unlock();
}

void Countly::setCountry(const std::string& country_code) {
	session_params["country_code"] = country_code;
}

void Countly::setCity(const std::string& city_name) {
	session_params["city"] = city_name;
}

void Countly::setLocation(double lattitude, double longitude) {
	std::ostringstream location_stream;
	location_stream << lattitude << ',' << longitude;
	session_params["location"] = location_stream.str();
}

void Countly::setDeviceID(const std::string& value, bool same_user) {
	mutex.lock();
	if (session_params.find("device_id") == session_params.end() || (session_params["device_id"].is_string() && session_params["device_id"].get<std::string>() == value)) {
		session_params["device_id"] = value;
		mutex.unlock();
		return;
	}

	if (same_user) {
		session_params["old_device_id"] = session_params["device_id"];
		session_params["device_id"] = value;
		if (began_session) {
			mutex.unlock();
			endSession();
			beginSession();
			mutex.lock();
		}
		session_params.erase("old_device_id");
	} else {
		if (began_session) {
			mutex.unlock();
			flushEvents();
			endSession();
			beginSession();
			mutex.lock();
		}
		session_params["device_id"] = value;
	}
	mutex.unlock();
}

void Countly::start(const std::string& app_key, const std::string& host, int port, bool start_thread) {
	mutex.lock();
	log(Countly::LogLevel::INFO, "[Countly][start]");
	this->host = host;
	if (host.find("http://") == 0) {
		use_https = false;
	} else if (host.find("https://") == 0) {
		use_https = true;
	} else {
		use_https = false;
		this->host.insert(0, "http://");
	}

	session_params["app_key"] = app_key;

	if (port <= 0) {
		this->port = use_https ? 443 : 80;
	} else {
		this->port = port;
	}

	if (!running) {

		mutex.unlock();
		beginSession();
		mutex.lock();

		if (start_thread) {
			stop_thread = false;

			try {
				if (thread == nullptr) {
					thread = new std::thread(&Countly::updateLoop, this);
				}
				
			} catch(const std::system_error& e) {
				std::ostringstream log_message;
				log_message << "Could not create thread: " << e.what();
				log(Countly::LogLevel::FATAL, log_message.str());
			}
		}
	}
	mutex.unlock();
}


/**
 * startOnCloud is deprecated and this is going to be removed in the future.
 */
void Countly::startOnCloud(const std::string& app_key) {
	log(Countly::LogLevel::WARNING, "[Countly][startOnCloud] 'startOnCloud' is deprecated, this is going to be removed in the future.");
	this->start(app_key, "https://cloud.count.ly", 443);
}

void Countly::stop() {
	_deleteThread();
	if (began_session && !is_dispose) {
		endSession();
	}
}

void Countly::_deleteThread() {
	mutex.lock();
	stop_thread = true;
	mutex.unlock();
	if (thread != nullptr && thread->joinable()) {
		try {
			thread->join();
		}
		catch (const std::system_error& e) {
			log(Countly::LogLevel::WARNING, "Could not join thread");
		}
		delete thread;
		thread = nullptr;
	}
}

void Countly::setUpdateInterval(size_t milliseconds) {
	mutex.lock();
	wait_milliseconds = milliseconds;
	mutex.unlock();
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
	if (database_path.empty()) {
		mutex.unlock();
		log(Countly::LogLevel::FATAL, "Cannot add event, sqlite database path is not set");
		return;
	}

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

void Countly::setMaxEvents(size_t value) {
	mutex.lock();
	max_events = value;
#ifndef COUNTLY_USE_SQLITE
	if (event_queue.size() > value) {
		log(Countly::LogLevel::WARNING, "New event queue size is smaller than the old one, dropping the oldest events to fit");
		event_queue.resize(value);
	}
#endif
	mutex.unlock();
}

void Countly::flushEvents(std::chrono::seconds timeout) {
	auto wait_duration = std::chrono::seconds(1);
	bool update_failed;
	while (timeout.count() != 0) {
#ifndef COUNTLY_USE_SQLITE
		mutex.lock();
		if (event_queue.empty()) {
			mutex.unlock();
			break;
		}
		mutex.unlock();

		update_failed = !updateSession();
#else
		mutex.lock();
		if (database_path.empty()) {
			mutex.unlock();
			log(Countly::LogLevel::FATAL, "Cannot flush events, sqlite database path is not set");
			return;
		}

		sqlite3 *database;
		int return_value, row_count, column_count;
		char** table;
		char *error_message;

		update_failed = true;
		mutex.lock();
		return_value = sqlite3_open(database_path.c_str(), &database);
		mutex.unlock();
		if (return_value == SQLITE_OK) {
			return_value = sqlite3_get_table(database, "SELECT COUNT(*) FROM events;", &table, &row_count, &column_count, &error_message);
			if (return_value == SQLITE_OK) {
				int event_count = atoi(table[1]);
				if (event_count > 0) {
					update_failed = !updateSession();
				}
			} else {
				log(Countly::LogLevel::ERROR, error_message);
				sqlite3_free(error_message);
			}
			sqlite3_free_table(table);
		}
		sqlite3_close(database);
#endif
		if (update_failed) {
			std::this_thread::sleep_for(wait_duration);
			wait_duration *= 2;
			timeout = (wait_duration > timeout) ? std::chrono::seconds(0) : (timeout - wait_duration);
		}
	}

#ifndef COUNTLY_USE_SQLITE
	event_queue.clear();
#else
	sqlite3 *database;
	int return_value;
	char *error_message;

	update_failed = true;
	return_value = sqlite3_open(database_path.c_str(), &database);
	if (return_value == SQLITE_OK) {
		return_value = sqlite3_exec(database, "DELETE FROM events;", nullptr, nullptr, &error_message);
		if (return_value != SQLITE_OK) {
			log(Countly::LogLevel::FATAL, error_message);
			sqlite3_free(error_message);
		}
	}
	sqlite3_close(database);
#endif
}

bool Countly::beginSession() {
	mutex.lock();
	log(Countly::LogLevel::INFO, "[Countly][beginSession]");
	if (began_session) {
		mutex.unlock();
		return true;
	}

	const std::chrono::system_clock::time_point now = Countly::getTimestamp();
	const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());

	std::map<std::string, std::string> data = {
		{"sdk_name", COUNTLY_SDK_NAME},
		{"sdk_version", COUNTLY_API_VERSION},
		{"timestamp", std::to_string(timestamp.count())},
		{"begin_session", "1"}
	};

	for (auto& element : session_params.items()) {
		if (element.value().is_string()) {
			data[element.key()] = element.value().get<std::string>();
		} else {
			data[element.key()] = element.value().dump();
		}
	}

	if (sendHTTP("/i", Countly::serializeForm(data)).success) {
		last_sent_session_request = Countly::getTimestamp();
		began_session = true;
	}

	if (remote_config_enabled) {
		mutex.unlock();
		updateRemoteConfig();
	} else {
		mutex.unlock();
	}

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

	json events = json::array();
	bool no_events;

#ifndef COUNTLY_USE_SQLITE
	no_events = event_queue.empty();
	if (!no_events) {
		for (const auto& event_json: event_queue) {
			events.push_back(json::parse(event_json));
		}
	}
#else
	if (database_path.empty()) {
		mutex.unlock();
		log(Countly::LogLevel::FATAL, "Cannot fetch events, sqlite database path is not set");
		return false;
	}

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
			event_id_stream << '(';

			for (int event_index = 1; event_index < row_count+1; event_index++) {
				event_id_stream << table[event_index * column_count] << ',';
				events.push_back(json::parse(table[(event_index * column_count) + 1]));
			}

			event_id_stream.seekp(-1, event_id_stream.cur);
			event_id_stream << ')';
			event_ids = event_id_stream.str();
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

	if (duration.count() >= _auto_session_update_interval) {
		log(Countly::LogLevel::DEBUG, "[Countly][updateSession] sending session update.");
		std::map<std::string, std::string> data = {
			{"app_key", session_params["app_key"].get<std::string>()},
			{"device_id", session_params["device_id"].get<std::string>()},
			{"session_duration", std::to_string(duration.count())}
		};
		if (!sendHTTP("/i", Countly::serializeForm(data)).success) {
			mutex.unlock();
			return false;
		}

		last_sent_session_request += duration;
	}

	if (!no_events) {
		log(Countly::LogLevel::DEBUG, "[Countly][updateSession] sending event.");
		std::map<std::string, std::string> data = {
		{"app_key", session_params["app_key"].get<std::string>()},
		{"device_id", session_params["device_id"].get<std::string>()},
		{"events", events.dump()}
		};

		if (!sendHTTP("/i", Countly::serializeForm(data)).success) {
			mutex.unlock();
			return false;
		}
	}

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

	mutex.unlock();
	return true;
}

bool Countly::endSession() {
	log(Countly::LogLevel::INFO, "[Countly][endSession]");
	const std::chrono::system_clock::time_point now = Countly::getTimestamp();
	const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
	const auto duration = std::chrono::duration_cast<std::chrono::seconds>(getSessionDuration(now));

	mutex.lock();
	std::map<std::string, std::string> data = {
		{"app_key", session_params["app_key"].get<std::string>()},
		{"device_id", session_params["device_id"].get<std::string>()},
		{"session_duration", std::to_string(duration.count())},
		{"timestamp", std::to_string(timestamp.count())},
		{"end_session", "1"}
	};
	if (sendHTTP("/i", Countly::serializeForm(data)).success) {
		last_sent_session_request = now;
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
	} else {
		log(Countly::LogLevel::ERROR, "Failed to open sqlite database");
		sqlite3_free(error_message);
		database_path.clear();
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

log(Countly::LogLevel::DEBUG, "[Countly][sendHTTP] data: "+ data);
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

	Countly::HTTPResponse response;
	response.success = false;

#ifdef COUNTLY_USE_CUSTOM_HTTP
	if (http_client_function == nullptr) {
		log(Countly::LogLevel::FATAL, "Missing HTTP client function");
		return response;
	}

	return http_client_function(use_post, path, data);
#else
	if (http_client_function != nullptr) {
		return http_client_function(use_post, path, data);
	}
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
		wchar_t *wide_path = new wchar_t[buffer_size];
		MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wide_path, buffer_size);

		hRequest = WinHttpOpenRequest(hConnect, use_post ? L"POST" : L"GET", wide_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, use_https ? WINHTTP_FLAG_SECURE : 0);
		delete wide_path;
	}

	if (hRequest) {
		bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, use_post ? (LPVOID)data.data() : WINHTTP_NO_REQUEST_DATA, use_post ? data.size() : 0, 0, 0) != 0;
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

						char *body_part = new char[n_bytes_available+1];
						memset(body_part, 0, n_bytes_available+1);
						DWORD n_bytes_read = 0;

						if (!WinHttpReadData(hRequest, body_part, n_bytes_available, &n_bytes_read)) {
							error_reading_body = true;
							delete body_part;
							break;
						}

						body += body_part;
						delete body_part;
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

		log(Countly::LogLevel::DEBUG, "[Countly][sendHTTP] request: " + full_url_stream.str());

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
	log(Countly::LogLevel::DEBUG, "[Countly][sendHTTP] response: " + response.data.dump());
	return response;
#endif

}

std::chrono::system_clock::duration Countly::getSessionDuration(std::chrono::system_clock::time_point now) {
	mutex.lock();
	std::chrono::system_clock::duration duration = now - last_sent_session_request;
	mutex.unlock();
	return duration;
}

std::chrono::system_clock::duration Countly::getSessionDuration() {
	return Countly::getSessionDuration(Countly::getTimestamp());
}

void Countly::updateLoop() {
	log(Countly::LogLevel::DEBUG, "[Countly][updateLoop]");
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
		size_t last_wait_milliseconds = wait_milliseconds;
		mutex.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(last_wait_milliseconds));
		updateSession();
	}
	mutex.lock();
	running = false;
	mutex.unlock();
}

void Countly::enableRemoteConfig() {
	mutex.lock();
	remote_config_enabled = true;
	mutex.unlock();
}

void Countly::updateRemoteConfig() {
	if (!session_params["app_key"].is_string() || !session_params["device_id"].is_string()) {
		log(Countly::LogLevel::ERROR, "Error updating remote config, app key or device id is missing");
		return;
	}

	std::map<std::string, std::string> data = {
		{"method", "fetch_remote_config"},
		{"app_key", session_params["app_key"].get<std::string>()},
		{"device_id", session_params["device_id"].get<std::string>()}
	};

	HTTPResponse response = sendHTTP("/o/sdk", serializeForm(data));
	if (response.success) {
		mutex.lock();
		remote_config = response.data;
		mutex.unlock();
	}
}

json Countly::getRemoteConfigValue(const std::string& key) {
	mutex.lock();
	json value = remote_config[key];
	mutex.unlock();
	return value;
}

void Countly::updateRemoteConfigFor(std::string *keys, size_t key_count) {
	std::map<std::string, std::string> data = {
		{"method", "fetch_remote_config"},
		{"app_key", session_params["app_key"].get<std::string>()},
		{"device_id", session_params["device_id"].get<std::string>()}
	};

	{
		json keys_json = json::array();
		for (size_t key_index = 0; key_index < key_count; key_index++) {
			keys_json.push_back(keys[key_index]);
		}
		data["keys"] = keys_json.dump();
	}

	HTTPResponse response = sendHTTP("/o/sdk", serializeForm(data));
	if (response.success) {
		mutex.lock();
		for (auto it = response.data.begin(); it != response.data.end(); ++it) {
			remote_config[it.key()] = it.value();
		}
		mutex.unlock();
	}
}

void Countly::updateRemoteConfigExcept(std::string *keys, size_t key_count) {
	std::map<std::string, std::string> data = {
		{"method", "fetch_remote_config"},
		{"app_key", session_params["app_key"].get<std::string>()},
		{"device_id", session_params["device_id"].get<std::string>()}
	};

	{
		json keys_json = json::array();
		for (size_t key_index = 0; key_index < key_count; key_index++) {
			keys_json.push_back(keys[key_index]);
		}
		data["omit_keys"] = keys_json.dump();
	}

	HTTPResponse response = sendHTTP("/o/sdk", serializeForm(data));
	if (response.success) {
		mutex.lock();
		for (auto it = response.data.begin(); it != response.data.end(); ++it) {
			remote_config[it.key()] = it.value();
		}
		mutex.unlock();
	}
}
