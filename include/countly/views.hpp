#pragma once
#include <string>
#include "countly.hpp"
using namespace std;

#include "nlohmann/json.hpp"
using json = nlohmann::json;

class Views {
	public:
		Views();
		~Views();
		void recordCloseView(const std::string& name);
		void recordAction(const std::string& type, int x, int y, int width, int height);
		void recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation);

	private:
		bool _isFirstView = true;
		std::map<std::string, double> _viewsStartTime;
};

