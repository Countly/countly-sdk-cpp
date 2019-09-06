#include <string>
#include <chrono>
#include <thread>
#include <deque>
#include <iostream>
#include <map>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "countly.hpp"
#include "doctest.h"

#define COUNTLY_TEST_APP_KEY "a32cb06789a6e99958d628378ee66bf8583a454f"
#define COUNTLY_TEST_DEVICE_ID "11732aa3-19a6-4272-9057-e3411f1938be"
#define COUNTLY_TEST_HOST "http://test.countly.notarealdomain"
#define COUNTLY_TEST_PORT 8080

struct HTTPCall {
	bool use_post;
	std::string url;
	std::map<std::string, std::string> data;
};

std::deque<HTTPCall> http_call_queue;

bool fakeSendHTTP(bool use_post, const std::string& url, const std::string& data) {
	HTTPCall http_call({use_post, url, {}});

	std::string::size_type startIndex = 0;
	for (auto seperatorIndex = data.find('&'); seperatorIndex != std::string::npos; seperatorIndex = data.find('&', startIndex)) {
		auto assignmentIndex = data.find('=', startIndex);
		if (assignmentIndex == std::string::npos || assignmentIndex >= seperatorIndex) {
			http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = "";
		} else {
			http_call.data[data.substr(startIndex, assignmentIndex - startIndex)] = data.substr(assignmentIndex + 1, seperatorIndex - assignmentIndex - 1);
		}
		startIndex = seperatorIndex + 1;
	}

	http_call_queue.push_back(http_call);
	return true;
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

#ifdef COUNTLY_USE_SQLITE
	countly.setDatabasePath("countly.db");
#endif

	countly.start(COUNTLY_TEST_APP_KEY, COUNTLY_TEST_DEVICE_ID, COUNTLY_TEST_HOST, COUNTLY_TEST_PORT);

	countly.beginSession();
	{
		HTTPCall http_call = popHTTPCall();
		CHECK(!http_call.use_post);
		CHECK(http_call.data["begin_session"] == "1");
	}

	{
		Countly::Event event("win", 4);
		event.addSegmentation("points", 100);
		countly.addEvent(event);
		std::this_thread::sleep_for(std::chrono::seconds(1));
		countly.updateSession();
		std::this_thread::sleep_for(std::chrono::seconds(1));

		HTTPCall http_call = popHTTPCall();
		CHECK(!http_call.use_post);
		CHECK(http_call.data["events"] == "%5B%7B%22key%22%3A%22win%22%2C%22count%22%3A4%2C%22segmentation%22%3A%7B%22points%22%3A100%7D%7D%5D");
	}
}
