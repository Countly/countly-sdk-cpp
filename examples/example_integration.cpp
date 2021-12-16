#include <thread>
#include <chrono>
#include <random>
#include <iostream>
using namespace std;


#include "countly.hpp"

void printLog(Countly::LogLevel level, const string& msg) {
	string lvl = "[DEBUG]";
	switch (level) {
	case Countly::LogLevel::DEBUG/* constant-expression */:
		lvl = "[Debug]";
		break;
	case Countly::LogLevel::INFO:
		lvl = "[INFO]";
		break;
		case Countly::LogLevel::WARNING:
		lvl = "[WARNING]";
		break;
		case Countly::LogLevel::ERROR:
		lvl = "[ERROR]";
		break;
		case Countly::LogLevel::FATAL:
		lvl = "[FATAL]";
		break;
	
	default:
		break;
	}
	
	cout<<lvl<<msg<<endl;
}



int main() {
	cout<<"Sample App"<<endl;
	Countly& ct = Countly::getInstance();
	ct.alwaysUsePost(true);
	ct.setDeviceID("zahidzafar");

	void (*logger_function)(Countly::LogLevel level, const std::string& message);
	logger_function = printLog;
	ct.setLogger(logger_function);
	// OS, OS_version, device, resolution, carrier, app_version);
	ct.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
	ct.setCustomUserDetails({{"Account Type", "Basic"}, {"Employer", "Company4"}});
	// Server and port
	ct.Start("8c1d653f8f474be24958b282d5e9b4c4209ee552", "https://master.count.ly", 443);
	ct.SetMaxEventsPerMessage(10);
	ct.SetMinUpdatePeriod(10);
	ct.setUpdateInterval(15);

	bool flag = true;
	while (flag) {
		cout<<"Choose your option:"<<endl;
		cout<<"1) Basic Event"<<endl;
		cout<<"2) Event with count and sum"<<endl;
		cout<<"3) Event with count, sum, duration"<<endl;
		cout<<"4) Event with sum, count, duration and segmentation"<<endl;
		cout<<"0) Exit"<<endl;
		int a;
		cin>>a;
		switch (a) {
		case 1:
			ct.RecordEvent("Basic Event", 123);
			break;
		case 2:
			ct.RecordEvent("Event with count and sum", 644, 13.3);
			break;
		case 3: {
			Countly::Event event("Event with sum, count, duration", 1, 10, 60.5);
			ct.addEvent(event);
			break;
		}
		case 4: {
			std::map<std::string, std::string> segmentation;
			segmentation["height"] = "5.10";
			ct.RecordEvent("Event with sum, count, duration and segmentation", segmentation, 1, 0, 10);
			break;
		}
		case 0:
			flag = false;
			break;
		
		default:
			cout<<"Option not found!"<<endl;
			break;
		}
	}

	ct.updateSession();

	return 0;
}
