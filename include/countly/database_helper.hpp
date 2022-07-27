#ifndef DATABASE_HELPER_HPP_
#define DATABASE_HELPER_HPP_

#include <memory>
#include "countly/logger_module.hpp"
#include "countly/event.hpp"

//template<class T>
class DatabaseHelper{
	public:

		DatabaseHelper();
		DatabaseHelper(LoggerModule* logger);

		~DatabaseHelper();

		bool storeEvent(const Event& event);
private:
	class DatabaseHelperImpl;
	std::unique_ptr<DatabaseHelperImpl> impl;
};
#endif

