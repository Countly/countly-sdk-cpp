#include <string>
#include <map>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "countly.hpp"
#include "doctest.h"

struct HTTPCall {
	bool is_post;
	std::string url;
	std::string data;
};

std::deque<HTTPCall> http_call_queue;

bool fakeSendHTTP(bool is_post, const std::string& url, const std::string& data) {
	http_call_queue.push_back({is_post, url, data});
	return true;
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
	countly.setHTTPClient(fakeSendHTTP);
}
