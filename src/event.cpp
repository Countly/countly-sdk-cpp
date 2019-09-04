#include "countly.hpp"

#include <sstream>

Countly::Event::Event(const std::string& key, size_t count) {
	std::ostringstream json_buffer;
	json_buffer << "{\"key\":\"" << key << "\",\"count\":" << count;
	json_start = json_buffer.str();
}

Countly::Event::Event(const std::string& key, size_t count, double sum) {
	std::ostringstream json_buffer;
	json_buffer << "{\"key\":\"" << key << "\",\"count\":" << count << ",\"sum\":" << sum;
	json_start = json_buffer.str();
}

template<>
void Countly::Event::addSegmentation<const char*>(const std::string& key, const char* value) {
	segmentation[key] = formatString(value);
}

template<>
void Countly::Event::addSegmentation<const std::string&>(const std::string& key, const std::string& value) {
	segmentation[key] = formatString(value);
}

template<>
void Countly::Event::addSegmentation<bool>(const std::string& key, bool value) {
	segmentation[key] = value ? "true" : "false";
}

std::string Countly::Event::serialize() const {
	std::ostringstream json_buffer;
	json_buffer << json_start;

	if (!segmentation.empty()) {
		json_buffer << ",\"segmentation\":{";

		for (const auto& key_value: segmentation) {
			json_buffer << formatString(key_value.first) << ':' << key_value.second << ',';
		}

		json_buffer.seekp(-1, json_buffer.cur);
		json_buffer << '}';
	}

	json_buffer << '}';

	return json_buffer.str();
}

std::string Countly::Event::formatString(const std::string& string) {
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
