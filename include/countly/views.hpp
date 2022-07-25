#ifndef VIEWS_HPP_
#define VIEWS_HPP_

#include <string>
#include"countly/event.hpp"

#include "nlohmann/json.hpp"
using json = nlohmann::json;
class Views {
	public:
		Views();
		~Views();
		static Views& getInstance();

		void recordCloseView(const std::string& name);
		void recordAction(const std::string& type, int x, int y, int width, int height);
		void recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation);

	private:
		
		bool _isFirstView = true;
		std::map<std::string, double> _viewsStartTime;
};
#endif

