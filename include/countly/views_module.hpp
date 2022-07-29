#ifndef VIEWS_MODULE_HPP_
#define VIEWS_MODULE_HPP_
#include <string>
#include <memory>
#include <map>
#include "countly/constants.hpp"
//#include "countly/logger_module.hpp"
//#include "countly/event.hpp"

namespace countly_sdk {
	class ViewsModule {
	public:
		class Event;
		class LoggerModule;
		~ViewsModule();
		ViewsModule(CountlyDelegates* cly, LoggerModule* logger);

		void recordCloseView(const std::string& name);
		void recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation);
	private:
		class ViewModuleImpl;
		std::unique_ptr<ViewModuleImpl> impl;
	};
}
#endif

