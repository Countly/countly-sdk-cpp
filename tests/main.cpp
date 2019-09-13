#include <string>
#include <chrono>
#include <thread>
#include <deque>
#include <iostream>
#include <cstdlib>
#include <map>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "countly.hpp"
#include "doctest.h"

#include "nlohmann/json.hpp"
using json = nlohmann::json;

#define COUNTLY_TEST_APP_KEY "a32cb06789a6e99958d628378ee66bf8583a454f"
#define COUNTLY_TEST_DEVICE_ID "11732aa3-19a6-4272-9057-e3411f1938be"
#define COUNTLY_TEST_HOST "http://test.countly.notarealdomain"
#define COUNTLY_TEST_PORT 8080

#ifdef COUNTLY_USE_SQLITE
#define WAIT_FOR_SQLITE(N) std::this_thread::sleep_for(std::chrono::seconds(N))
#else
#define WAIT_FOR_SQLITE(N)
#endif

void decodeURL(std::string& encoded) {
	for (auto percent_index = encoded.find('%'); percent_index != std::string::npos; percent_index = encoded.find('%', percent_index + 1)) {
		std::string hex_string = encoded.substr(percent_index + 1, 2);
		char value = std::strtol(hex_string.c_str(), nullptr, 16);
		encoded.replace(percent_index, 3, std::string(1, value));
	}
}

struct HTTPCall {
	bool use_post;
	std::string url;
	std::map<std::string, std::string> data;
};

std::deque<HTTPCall> http_call_queue;

Countly::HTTPResponse fakeSendHTTP(bool use_post, const std::string& url, const std::string& data) {
	HTTPCall http_call({use_post, url, {}});

	std::string::size_type startIndex = 0;
	for (auto seperatorIndex = data.find('&'); seperatorIndex != std::string::npos; seperatorIndex = data.find('&', startIndex)) {
		auto assignmentIndex = data.find('=', startIndex);
		if (assignmentIndex == std::string::npos || assignmentIndex >= seperatorIndex) {
			http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = "";
		} else {
			http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = data.substr(assignmentIndex + 1, seperatorIndex - assignmentIndex - 1);
			decodeURL(http_call.data[data.substr(startIndex, assignmentIndex - startIndex)]);
		}
		startIndex = seperatorIndex + 1;
	}

	http_call_queue.push_back(http_call);
	Countly::HTTPResponse response;
	response.success = true;
	return response;
}

void logToConsole(Countly::LogLevel level, const std::string& message) {
	std::cout << level << '\t' << message << std::endl;
}

HTTPCall popHTTPCall() {
	CHECK(!http_call_queue.empty());
	HTTPCall oldest_call = http_call_queue.front();
	http_call_queue.pop_front();
	return oldest_call;
}

TEST_CASE("urlencoding is correct") {
	CHECK(Countly::encodeURL("hello world") == "hello%20world");
	CHECK(Countly::encodeURL("{\"key\":\"win\",\"count\":3}") == "%7B%22key%22%3A%22win%22%2C%22count%22%3A3%7D");
}

TEST_CASE("forms are serialized correctly") {
	CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key1", "value1"}, {"key2", "value2"}})) == "key1=value1&key2=value2");
	CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key", "hello world"}})) == "key=hello%20world");
}

TEST_CASE("events are sent correctly") {
	Countly& countly = Countly::getInstance();
	countly.setLogger(logToConsole);
	countly.setHTTPClient(fakeSendHTTP);
	countly.setDeviceID(COUNTLY_TEST_DEVICE_ID);

#ifdef COUNTLY_USE_SQLITE
	countly.setDatabasePath("countly-test.db");
#endif

	countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT, false);

	SUBCASE("session begins") {
		countly.beginSession();
		HTTPCall http_call = popHTTPCall();
		CHECK(!http_call.use_post);
		CHECK(http_call.data["begin_session"] == "1");
	}

	SUBCASE("single event is sent") {
		Countly::Event event("win", 4);
		event.addSegmentation("points", 100);
		countly.addEvent(event);
		WAIT_FOR_SQLITE(1);
		countly.updateSession();

		HTTPCall http_call = popHTTPCall();
		CHECK(!http_call.use_post);
		CHECK(http_call.data["events"] == "[{\"count\":4,\"key\":\"win\",\"segmentation\":{\"points\":100}}]");
	}

	SUBCASE("two events are sent") {
		Countly::Event event1("win", 2);
		Countly::Event event2("achievement", 1);
		countly.addEvent(event1);
		countly.addEvent(event2);
		WAIT_FOR_SQLITE(1);
		countly.updateSession();

		HTTPCall http_call = popHTTPCall();
		CHECK(!http_call.use_post);
		CHECK(http_call.data["events"] == "[{\"count\":2,\"key\":\"win\"},{\"count\":1,\"key\":\"achievement\"}]");
	}

	SUBCASE("100 events are sent") {
		for (int i = 0; i < 100; i++) {
			Countly::Event event("click", i);
			countly.addEvent(event);
		}
		WAIT_FOR_SQLITE(1);
		countly.updateSession();

		HTTPCall http_call = popHTTPCall();
		CHECK(http_call.use_post);
	}
}
