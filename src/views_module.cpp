#include "countly/views_module.hpp"
#include <iostream>
#include <map>


#define CLY_VIEW_KEY "[CLY]_view"


class ViewsModule :: ViewModuleImpl {
	
public:
	bool _isFirstView = true;
	std::map<std::string, double> _viewsStartTime;
	
	
};


ViewsModule::ViewsModule() : impl{ std::make_unique<ViewsModule::ViewModuleImpl>() }
{
	
}

ViewsModule::~ViewsModule() {
}

void ViewsModule::foo(const std::string& name) {
	std::cout << "ViewsModule::foo " + name << std::endl;
}
//
//Event Views::recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation) {
//	
//
//	if (_viewsStartTime.find(name) == _viewsStartTime.end()) {
//		_viewsStartTime["name"] = 5.0;
//	}
//
//	Event event(CLY_VIEW_KEY, 1);
//
//	event.addSegmentation("name", name);
//	event.addSegmentation("segment", "cpp");
//	event.addSegmentation("visit", 1);
//	event.addSegmentation("start", _isFirstView ? 1 : 0);
//
//	for (auto key_value : segmentation) {
//		event.addSegmentation(key_value.first, key_value.second);
//	}
//
//	//return event;
//}
//
//Event Views::recordCloseView(const std::string& name) {
//	if (_viewsStartTime.find(name) != _viewsStartTime.end()) {
//		double duration = _viewsStartTime[name];
//
//		Countly::Event event(CLY_VIEW_KEY, 1, 0, duration);
//
//		event.addSegmentation("name", name);
//		event.addSegmentation("segment", "cpp");
//
//		_viewsStartTime.erase(name);
//
//		//return event;
//	}
//
//	//return nullptr;
//	
//}
//
//Event Views::recordAction(const std::string& type, int x, int y, int width, int height) {
//	Countly::Event event(CLY_VIEW_KEY, 1);
//	
//	event.addSegmentation("x", x);
//	event.addSegmentation("y", y);
//	event.addSegmentation("type", type);
//	event.addSegmentation("width", width);
//	event.addSegmentation("height", height);
//
//	//return event;
//}