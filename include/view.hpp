#pragma once
#include <string>


#include "nlohmann/json.hpp"
using json = nlohmann::json;


class View {
public:
	View(const std::string& name);
	

	std::string serialize() const;
private:
	json object;

	template<typename T>
	void addSegmentation(const std::string& key, T value) {
		if (object.find("segmentation") == object.end()) {
			object["segmentation"] = json::object();
		}

		object["segmentation"][key] = value;
	}
};
