#include "countly/logger_module.hpp"
#include <iostream>
namespace cly {
	class LoggerModule::LoggerModuleImpl {
	public:
		LoggerFunction logger_function;

	};

	LoggerModule::LoggerModule()
	{
		impl.reset(new LoggerModuleImpl());
	}

	LoggerModule::~LoggerModule() {
	}

	void LoggerModule::setLogger(LoggerFunction logger) {
		impl->logger_function = logger;
	}

	void LoggerModule::log(LogLevel level, const std::string& message) {
		if (impl->logger_function != nullptr) {
			impl->logger_function(level, message);
		}
	}
}

