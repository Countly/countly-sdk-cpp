//
//  CountlyEventQueue.cpp
//  CountlyCpp
//
//  Created by Noth on 26/10/14.
//  Copyright (c) 2014 Gith Security Systems. All rights reserved.
//

#include "CountlyEventQueue.h"
#include <sstream>
#include <iomanip>

#include "Countly.h"
#include "assert.h"

using namespace std;
/*
 
 [
 {
   "key": "level_success",
   "count": 4
 },
 {
   "key": "level_fail",
   "count": 2
 },
 {
   "key": "in_app_purchase",
   "count": 3,
   "sum": 2.97,
   "segmentation": {
     "app_version": "1.0",
     "country": "Turkey"
   }
 }
 ]
 
 */
namespace CountlyCpp
{
  
  
  CountlyEventQueue::CountlyEventQueue()
  {
    if (sqlite3_open("countly.sqlite", &_sqlHandler) != SQLITE_OK)
      return;
    
    if (!sqlite3_threadsafe())
    {
      assert(0);
    }
    
    char *zErrMsg = NULL;
    string req = "CREATE TABLE IF NOT EXISTS events (evtid INTEGER PRIMARY KEY, event TEXT)";
    unsigned int code = sqlite3_exec(_sqlHandler, req.c_str(), NULL, 0, &zErrMsg);
    if (code != SQLITE_OK)
    {
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        cout << "Failed to setup sqlite" << endl;
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
    }
    
    req = "CREATE TABLE IF NOT EXISTS settings (deviceid TEXT)";
    code = sqlite3_exec(_sqlHandler, req.c_str(), NULL, 0, &zErrMsg);
    if (code != SQLITE_OK)
    {
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        cout << "Failed to setup sqlite" << endl;
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
    }

    
    
    pthread_mutexattr_t mutAttr;
    pthread_mutexattr_init(&mutAttr);
    pthread_mutex_init(&_lock, &mutAttr);
    
  }
  
  CountlyEventQueue::~CountlyEventQueue()
  {
    if (_sqlHandler)
      sqlite3_close(_sqlHandler);
    pthread_mutex_destroy(&_lock);
  }
  


  void CountlyEventQueue::RecordEvent(std::string key, int count)
  {
    stringstream json;
    json << "{\n";
    json << "  \"timestamp\": \"" << dec << Countly::GetTimestamp() << "\",\n";
    json << "  \"key\": \"" << key << "\",\n";
    json << "  \"count\": " << dec << count << "\n";
    json << "}";
    AddEvent(json.str());
  }
  
  void CountlyEventQueue::RecordEvent(std::string key, int count, double sum)
  {
    stringstream json;
    json << "{\n";
    json << "  \"timestamp\": \"" << dec << Countly::GetTimestamp() << "\",\n";
    json << "  \"key\": \"" << key << "\",\n";
    json << "  \"count\": " << dec << count << ",\n";
    json << "  \"sum\": \"" << dec << sum << "\"\n";
    json << "}";
    AddEvent(json.str());
  }
  
  void CountlyEventQueue::RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count)
  {
    stringstream json;
    std::map<std::string, std::string>::iterator it;
    json << "{\n";
    json << "  \"timestamp\": \"" << dec << Countly::GetTimestamp() << "\",\n";
    json << "  \"key\": \"" << key << "\",\n";
    json << "  \"count\": " << dec << count << ",\n";

    json << "  \"segmentation\": {\n";

    it = segmentation.begin();
    while (it != segmentation.end())
    {
      json << "    \"" << it->first << "\": \"" << it->second << "\"" << (it == segmentation.end() ? "":",")<< "\n";
      it++;
    }
    json << "  }\n";
    json << "}";
    AddEvent(json.str());
  }
  
  void CountlyEventQueue::RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count, double sum)
  {
    stringstream json;
    std::map<std::string, std::string>::iterator it;

    json << "{\n";
    json << "  \"timestamp\": \"" << dec << Countly::GetTimestamp() << "\",\n";
    json << "  \"key\": \"" << key << "\",\n";
    json << "  \"count\": " << dec << count << ",\n";
    json << "  \"sum\": \"" << dec << sum << "\",\n";
    json << "  \"segmentation\": {\n";
    
    it = segmentation.begin();
    while (it != segmentation.end())
    {
      json << "    \"" << it->first << "\": \"" << it->second << "\"" << (it == segmentation.end() ? "":",")<< "\n";
      it++;
    }
    json << "  }\n";
    json << "}";
    AddEvent(json.str());
  }

  void CountlyEventQueue::AddEvent(std::string json)
  {
    if (!_sqlHandler)
      return;
    
    pthread_mutex_lock(&_lock);
    
    char *zErrMsg = NULL;
    string req = "INSERT INTO events (event) VALUES('" + json +"')";
    unsigned int code = sqlite3_exec(_sqlHandler, req.c_str(), NULL, 0, &zErrMsg);
    
    if (code != SQLITE_OK)
    {
      cout << "Failed to exec " << req << " :" << zErrMsg<< endl;
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
    }
    pthread_mutex_unlock(&_lock);

  }
  
  std::string CountlyEventQueue::GetDeviceId()
  {
    string deviceid;
    char *zErrMsg = NULL;
    char **pazResult;
    int rows, nbCols;
    
    if (!_sqlHandler)
      return 0;
    
      //Read deviceid from settings
    pthread_mutex_lock(&_lock);
    string req = "SELECT deviceid FROM settings";
    unsigned int code = sqlite3_get_table(_sqlHandler, req.c_str(), &pazResult, &rows, &nbCols, &zErrMsg);
    if ((code == SQLITE_OK) && (rows))
    {
      deviceid = pazResult[1];
      sqlite3_free_table(pazResult);
      pthread_mutex_unlock(&_lock);
      cout << "Loaded UDID " << deviceid << endl;
      return deviceid;
    }
    sqlite3_free_table(pazResult);
    pthread_mutex_unlock(&_lock);
    
      //Failed, create new one
    stringstream UDID;
    unsigned long long seed = Countly::GetTimestamp();
    srand(seed);
    for (int i = 0; i < 20 / sizeof(int); i++)
      UDID << setfill ('0') << setw(8) << hex << rand();
    deviceid = UDID.str();
    cout << "Created UDID " << deviceid << endl;
    req = "INSERT INTO settings (deviceid) VALUES('" + deviceid + "')";
    pthread_mutex_lock(&_lock);
    code = sqlite3_exec(_sqlHandler, req.c_str(), NULL, 0, &zErrMsg);
    if (code != SQLITE_OK)
    {
      cout << "Failed to exec " << req << " :" << zErrMsg<< endl;
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
    }
    pthread_mutex_unlock(&_lock);

    return deviceid;
  }
  
  int CountlyEventQueue::Count()
  {
    int ret = 0;
    char *zErrMsg = NULL;
    char **pazResult;
    int rows, nbCols;
    
    if (!_sqlHandler)
      return 0;
    
    pthread_mutex_lock(&_lock);
    string req = "SELECT COUNT(*) FROM events";
    unsigned int code = sqlite3_get_table(_sqlHandler, req.c_str(), &pazResult, &rows, &nbCols, &zErrMsg);
    if (code != SQLITE_OK)
    {
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
      pthread_mutex_unlock(&_lock);
      return 0;
    }

    if (rows != 0)
      ret = atoi(pazResult[1]);
    sqlite3_free_table(pazResult);
    pthread_mutex_unlock(&_lock);

    return ret;
  }
  
  std::string CountlyEventQueue::PopEvent(int * evtId)
  {
    string ret;
    char *zErrMsg = NULL;
    char **pazResult;
    int rows, nbCols;
    *evtId = -1;
    pthread_mutex_lock(&_lock);
    
    string req = "SELECT evtid, event FROM events LIMIT 1";
    unsigned int code = sqlite3_get_table(_sqlHandler, req.c_str(), &pazResult, &rows, &nbCols, &zErrMsg);
    if (code != SQLITE_OK)
    {
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
      pthread_mutex_unlock(&_lock);
      
      return "";
    }
    
    if (!rows)
    {
      pthread_mutex_unlock(&_lock);
      return "";
    }

      //pasResult[0] : "evtid"
      //pasResult[1] : "event"
    *evtId = atoi(pazResult[2]);
    ret = pazResult[3];
    sqlite3_free_table(pazResult);
    pthread_mutex_unlock(&_lock);
    
    cout << "Loaded evt " << *evtId << " : " << ret << endl;
    return ret;
  }
  
  void CountlyEventQueue::ClearEvent(int evtId)
  {
    stringstream req;
    req  << "DELETE FROM events WHERE evtid=" << dec << evtId;
    pthread_mutex_lock(&_lock);
    char *zErrMsg = NULL;

    unsigned int code = sqlite3_exec(_sqlHandler, req.str().c_str(), NULL, 0, &zErrMsg);
    
    if (code != SQLITE_OK)
    {
      if ((code == SQLITE_CORRUPT) || (code == SQLITE_IOERR_SHORT_READ) || (code == SQLITE_IOERR_WRITE) || (code == SQLITE_IOERR))
      {
        cout << "Failed to clear event" << endl;
        sqlite3_close(_sqlHandler);
        _sqlHandler = NULL;
      }
    }
    pthread_mutex_unlock(&_lock);
    cout << "Cleared evt " << evtId << " : " << req.str() << endl;

  }
  
  
  
}


#if 0

@interface CountlyEventQueue : NSObject

@end


@implementation CountlyEventQueue

- (void)dealloc
{
  [super dealloc];
}

- (NSUInteger)count
{
  @synchronized (self)
  {
    return [[CountlyDB sharedInstance] getEventCount];
  }
}


- (NSString *)events
{
  NSMutableArray* result = [NSMutableArray array];
  
	@synchronized (self)
  {
		NSArray* events = [[[[CountlyDB sharedInstance] getEvents] copy] autorelease];
		for (id managedEventObject in events)
    {
			CountlyEvent* event = [CountlyEvent objectWithManagedObject:managedEventObject];
      
			[result addObject:event.serializedData];
      
      [CountlyDB.sharedInstance deleteEvent:managedEventObject];
    }
  }
  
	return CountlyURLEscapedString(CountlyJSONFromObject(result));
}

- (void)recordEvent:(NSString *)key count:(int)count
{
  @synchronized (self)
  {
    NSArray* events = [[[[CountlyDB sharedInstance] getEvents] copy] autorelease];
    for (NSManagedObject* obj in events)
    {
      CountlyEvent *event = [CountlyEvent objectWithManagedObject:obj];
      if ([event.key isEqualToString:key])
      {
        event.count += count;
        event.timestamp = (event.timestamp + time(NULL)) / 2;
        
        [obj setValue:@(event.count) forKey:@"count"];
        [obj setValue:@(event.timestamp) forKey:@"timestamp"];
        
        [[CountlyDB sharedInstance] saveContext];
        return;
      }
    }
    
    CountlyEvent *event = [[CountlyEvent new] autorelease];
    event.key = key;
    event.count = count;
    event.timestamp = time(NULL);
    
    [[CountlyDB sharedInstance] createEvent:event.key count:event.count sum:event.sum segmentation:event.segmentation timestamp:event.timestamp];
  }
}

- (void)recordEvent:(NSString *)key count:(int)count sum:(double)sum
{
  @synchronized (self)
  {
    NSArray* events = [[[[CountlyDB sharedInstance] getEvents] copy] autorelease];
    for (NSManagedObject* obj in events)
    {
      CountlyEvent *event = [CountlyEvent objectWithManagedObject:obj];
      if ([event.key isEqualToString:key])
      {
        event.count += count;
        event.sum += sum;
        event.timestamp = (event.timestamp + time(NULL)) / 2;
        
        [obj setValue:@(event.count) forKey:@"count"];
        [obj setValue:@(event.sum) forKey:@"sum"];
        [obj setValue:@(event.timestamp) forKey:@"timestamp"];
        
        [[CountlyDB sharedInstance] saveContext];
        
        return;
      }
    }
    
    CountlyEvent *event = [[CountlyEvent new] autorelease];
    event.key = key;
    event.count = count;
    event.sum = sum;
    event.timestamp = time(NULL);
    
    [[CountlyDB sharedInstance] createEvent:event.key count:event.count sum:event.sum segmentation:event.segmentation timestamp:event.timestamp];
  }
}

- (void)recordEvent:(NSString *)key segmentation:(NSDictionary *)segmentation count:(int)count;
{
  @synchronized (self)
  {
    NSArray* events = [[[[CountlyDB sharedInstance] getEvents] copy] autorelease];
    for (NSManagedObject* obj in events)
    {
      CountlyEvent *event = [CountlyEvent objectWithManagedObject:obj];
      if ([event.key isEqualToString:key] &&
          event.segmentation && [event.segmentation isEqualToDictionary:segmentation])
      {
        event.count += count;
        event.timestamp = (event.timestamp + time(NULL)) / 2;
        
        [obj setValue:@(event.count) forKey:@"count"];
        [obj setValue:@(event.timestamp) forKey:@"timestamp"];
        
        [[CountlyDB sharedInstance] saveContext];
        
        return;
      }
    }
    
    CountlyEvent *event = [[CountlyEvent new] autorelease];
    event.key = key;
    event.segmentation = segmentation;
    event.count = count;
    event.timestamp = time(NULL);
    
    [[CountlyDB sharedInstance] createEvent:event.key count:event.count sum:event.sum segmentation:event.segmentation timestamp:event.timestamp];
  }
}

- (void)recordEvent:(NSString *)key segmentation:(NSDictionary *)segmentation count:(int)count sum:(double)sum;
{
  @synchronized (self)
  {
    NSArray* events = [[[[CountlyDB sharedInstance] getEvents] copy] autorelease];
    for (NSManagedObject* obj in events)
    {
      CountlyEvent *event = [CountlyEvent objectWithManagedObject:obj];
      if ([event.key isEqualToString:key] &&
          event.segmentation && [event.segmentation isEqualToDictionary:segmentation])
      {
        event.count += count;
        event.sum += sum;
        event.timestamp = (event.timestamp + time(NULL)) / 2;
        
        [obj setValue:@(event.count) forKey:@"count"];
        [obj setValue:@(event.sum) forKey:@"sum"];
        [obj setValue:@(event.timestamp) forKey:@"timestamp"];
        
        [[CountlyDB sharedInstance] saveContext];
        
        return;
      }
    }
    
    CountlyEvent *event = [[CountlyEvent new] autorelease];
    event.key = key;
    event.segmentation = segmentation;
    event.count = count;
    event.sum = sum;
    event.timestamp = time(NULL);
    
    [[CountlyDB sharedInstance] createEvent:event.key count:event.count sum:event.sum segmentation:event.segmentation timestamp:event.timestamp];
  }
}

@end


#endif
