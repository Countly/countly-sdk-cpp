#include "countly/views_module.hpp"

#define CLY_VIEW_KEY "[CLY]_view"

namespace countly_sdk {
	class ViewsModule::ViewModuleImpl {

	public:

		ViewModuleImpl(CountlyDelegates* cly, LoggerModule* logger) : mCly{ cly }, mLogger{ logger } {
		}

		bool _isFirstView = true;
		std::map<std::string, double> _viewsStartTime;

		LoggerModule* mLogger;
		CountlyDelegates* mCly;
	};


	ViewsModule::ViewsModule(CountlyDelegates* cly, LoggerModule* logger) {
		impl.reset(new ViewModuleImpl(cly, logger));
		//impl->mLogger;->log(0, "ViewsModule:: Initialized");
	}

	ViewsModule::~ViewsModule() {
	}

	void ViewsModule::recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation) {
		if (impl->_viewsStartTime.find(name) == impl->_viewsStartTime.end()) {
			impl->_viewsStartTime["name"] = 5.0;
		}
		

		std::map<std::string, std::string> viewSegments;

		viewSegments["name"] = name;
		viewSegments["segment"] = "cpp";
		viewSegments["visit"] = 1;
		viewSegments["start"] = impl->_isFirstView ? 1 : 0;

		for (auto key_value : segmentation) {

			auto itr = viewSegments.find(key_value.first);
			if (itr != viewSegments.end()) {
				(*itr).second = key_value.second;
			}
			else {
				viewSegments[key_value.first] = key_value.second;
			}
		}

		impl->mCly->recordEventInternal(CLY_VIEW_KEY, viewSegments);

		

		//impl->mLogger->log(0, "ViewsModule:: recordOpenView event: " + event.serialize());

	}

	void ViewsModule::recordCloseView(const std::string& name) {
		if (impl->_viewsStartTime.find(name) != impl->_viewsStartTime.end()) {
			double duration = impl->_viewsStartTime[name];

			std::map<std::string, std::string> viewSegments;

			viewSegments["name"] = name;
			viewSegments["segment"] = "cpp";

			impl->mCly->recordEventInternal(CLY_VIEW_KEY, viewSegments);
			impl->_viewsStartTime.erase(name);
		}
		else {
			//Print error
			//impl->mLogger->log(0, "ViewsModule:: recordOpenView event: " + event.serialize());
		}

	}
}
