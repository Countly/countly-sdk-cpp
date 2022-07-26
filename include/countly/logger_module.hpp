
#ifndef LOGGER_MODULE_HPP_
#define LOGGER_MODULE_HPP_
#include <string>
#include <memory>
#include <functional>

//template<class T>
class LoggerModule{
	public:
		//template<class T>
		using LoggerFunction = std::function<void(int, const std::string&)>;
		
		//using LoggerFunction = std::function<void(T, const std::string&)>;

		LoggerModule();
		~LoggerModule();
		void setLogger(LoggerFunction logger);
		void log(int level, const std::string& message);

		void foo(const std::string& name);
private:
	class LoggerModuleImpl;
	std::unique_ptr<LoggerModuleImpl> impl;
};
#endif

