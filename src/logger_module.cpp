#include <iostream>

#include "countly/logger_module.hpp"



class LoggerModule::LoggerModuleImpl {
public:
	LoggerFunction logger_function;

};


LoggerModule::LoggerModule() : impl{ std::make_unique<LoggerModule::LoggerModuleImpl>() }
{

}

LoggerModule::~LoggerModule() {
}

void LoggerModule::setLogger(LoggerFunction logger) {
	impl->logger_function = logger;
}

void LoggerModule::log(int level, const std::string& message) {
	if (impl->logger_function) {
		impl->logger_function(level, message);
	}
}

void LoggerModule::foo(const std::string& name) {
	std::cout << "LoggerModule::foo " + name << std::endl;
}
