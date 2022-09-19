#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "countly.hpp"
#include "doctest.h"

#include "nlohmann/json.hpp"
#include "test_utils.hpp"
using json = nlohmann::json;
using namespace cly;

TEST_CASE("urlencoding is correct") {
  CHECK(Countly::encodeURL("hello world") == "hello%20world");
  CHECK(Countly::encodeURL("hello.~world") == "hello.~world");
  CHECK(Countly::encodeURL("{\"key\":\"win\",\"count\":3}") == "%7B%22key%22%3A%22win%22%2C%22count%22%3A3%7D");
  CHECK(Countly::encodeURL("测试") == "%E6%B5%8B%E8%AF%95");
}
#ifdef COUNTLY_USE_CUSTOM_SHA256

std::string customChecksumCalculator(const std::string &data) {
  std::string result = data.c_str();
  result.append("-custom_sha");
  return result;
}

TEST_CASE("custom sha256 function validation") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  std::string salt = "test-salt";
  std::string checksum = countly.calculateChecksum(salt, "hello world:");
  CHECK(checksum == ""); // when customSha256 isn't set.

  countly.setSha256(customChecksumCalculator);
  salt = "test-salt";
  checksum = countly.calculateChecksum(salt, "hello world:");
  CHECK(checksum == "hello world:test-salt-custom_sha");

  salt = "š ūļ ķ";
  checksum = countly.calculateChecksum(salt, "测试:");
  CHECK(checksum == "测试:š ūļ ķ-custom_sha");
}
#else
TEST_CASE("checksum function validation") {
  Countly &countly = Countly::getInstance();
  std::string salt = "test-salt";
  std::string checksum = countly.calculateChecksum(salt, "hello world");
  CHECK(checksum == "aaf992c81357b0ed1bb404826e01825568126ebeb004c3bc690d3d8e0766a3cc");

  salt = "š ūļ ķ";
  checksum = countly.calculateChecksum(salt, "测试");
  CHECK(checksum == "f51d24b0cb938e2f40b1f8609c62bf2508e24bcaa3b6b1a7fbf108d3c7f2f073");
}
#endif

void printLog(LogLevel level, const std::string &msg) {
  CHECK(msg == "message");
  CHECK(level == LogLevel::DEBUG);
}

TEST_CASE("Logger function validation") {
  clearSDK();
  Countly &countly = Countly::getInstance();

  CHECK(countly.getLogger() == nullptr);
  countly.setLogger(printLog);
  CHECK(countly.getLogger() != nullptr);

  countly.getLogger()(LogLevel::DEBUG, "message");

  countly.setLogger(nullptr);
  CHECK(countly.getLogger() == nullptr);
}

TEST_CASE("forms are serialized correctly") {
  CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key1", "value1"}, {"key2", "value2"}})) == "key1=value1&key2=value2");
  CHECK(Countly::serializeForm(std::map<std::string, std::string>({{"key", "hello world"}})) == "key=hello%20world");
}