

 #include "countly/database_helper.hpp"



 class DatabaseHelper::DatabaseHelperImpl {
 public:
	 DatabaseHelperImpl() {
		 mLogger = nullptr;
	 }

	 DatabaseHelperImpl(LoggerModule* logger) : mLogger{ logger }
	 {
	 }

	 LoggerModule* mLogger;
 };


 DatabaseHelper::DatabaseHelper(LoggerModule* logger) : impl{ std::make_unique<DatabaseHelperImpl>(logger) }
 {
	 impl->mLogger->log(0, "DatabaseHelper:: Initialized");
 }

 DatabaseHelper::DatabaseHelper() : impl{ std::make_unique<DatabaseHelperImpl>() }
 {

 }

 DatabaseHelper::~DatabaseHelper() {
 }

 bool DatabaseHelper::storeEvent(const Event& event) {
	 impl->mLogger->log(0, "DatabaseHelper:: storeEvent event: " + event.serialize());

	 return true;
 }

 
