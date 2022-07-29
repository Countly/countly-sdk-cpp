#ifndef VIEWS_MODULE_HPP_
#define VIEWS_MODULE_HPP_
#include <string>
#include <memory>
#include <map>
#include "countly/logger_module.hpp"
#include "countly/event.hpp"
#include "countly/database_helper.hpp"

class ViewsInterface {
public:
	virtual void foo(const std::string& name) = 0;
	/*void recordCloseView(const std::string& name);
	void recordAction(const std::string& type, int x, int y, int width, int height);
	void recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation);*/
};


class ViewsModule {
public:
	ViewsModule();
	ViewsModule(LoggerModule* logger, DatabaseHelper* databaseHelper);

	~ViewsModule();

	void foo(const std::string& name);
	void recordCloseView(const std::string& name);
	void recordOpenView(const std::string& name, std::map<std::string, std::string> segmentation);
private:
	class ViewModuleImpl;
	std::unique_ptr<ViewModuleImpl> impl;
};
#endif

