#include "countly/views_module.hpp"

#define CLY_VIEW_KEY "[CLY]_view"


class ViewsModule::ViewModuleImpl {

public:
	ViewModuleImpl() {
		mLogger = nullptr;
	}

	ViewModuleImpl(LoggerModule* logger) : mLogger{ logger }
	{
	}

	bool _isFirstView = true;
	LoggerModule* mLogger;
	std::map<std::string, double> _viewsStartTime;

};


ViewsModule::ViewsModule(LoggerModule* logger) : impl{ std::make_unique<ViewModuleImpl>(logger) }
{
	impl->mLogger->log(0, "ViewsModule:: Initialized");
}

ViewsModule::ViewsModule() : impl{ std::make_unique<ViewModuleImpl>() }
{
	impl->mLogger->log(0, "ViewsModule:: Initialized");
}

ViewsModule::~ViewsModule() {
}

void ViewsModule::foo(const std::string& name) {
	impl->mLogger->log(0, "ViewsModule:: foo()");
}

void ViewsModule::recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation) {
	if (impl->_viewsStartTime.find(name) == impl->_viewsStartTime.end()) {
		impl->_viewsStartTime["name"] = 5.0;
	}

	Event event(CLY_VIEW_KEY, 1);

	event.addSegmentation("name", name);
	event.addSegmentation("segment", "cpp");
	event.addSegmentation("visit", 1);
	event.addSegmentation("start", impl->_isFirstView ? 1 : 0);

	for (auto key_value : segmentation) {
		event.addSegmentation(key_value.first, key_value.second);
	}

	impl->mLogger->log(0, "ViewsModule:: recordOpenView event: " + event.serialize());
}

void ViewsModule::recordCloseView(const std::string& name) {
	if (impl->_viewsStartTime.find(name) != impl->_viewsStartTime.end()) {
		double duration = impl->_viewsStartTime[name];

		Event event(CLY_VIEW_KEY, 1, 0, duration);

		event.addSegmentation("name", name);
		event.addSegmentation("segment", "cpp");

		impl->_viewsStartTime.erase(name);
		impl->mLogger->log(0, "ViewsModule:: recordCloseView event: " + event.serialize());
	}

}