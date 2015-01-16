/*
 CountlyEventQueue.cpp
 CountlyCpp
 
 Created by Benoit Girard on 26/10/14.
 
 The MIT License (MIT)
 
 Copyright (c) 2014 Gith Security Systems
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "CountlyEventQueue.h"
#include <sstream>
#include <iomanip>
#include <stdlib.h>

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
  
  
  CountlyEventQueue::CountlyEventQueue() :
  _sqlHandler(NULL)
  {
    pthread_mutexattr_t mutAttr;
    pthread_mutexattr_init(&mutAttr);
    pthread_mutex_init(&_lock, &mutAttr);
    _path = "";
  }
  
  CountlyEventQueue::~CountlyEventQueue()
  {
    if (_sqlHandler)
      sqlite3_close(_sqlHandler);
    pthread_mutex_destroy(&_lock);
  }
  
  void CountlyEventQueue::SetPath(std::string path)
  {
    _path = path;
  }


  void CountlyEventQueue::LoadDb()
  {
    assert(_path.size());
    
    pthread_mutex_lock(&_lock);
    if (_sqlHandler)
    {
      pthread_mutex_unlock(&_lock);
      return;
    }
    
    string fullpath = _path + string("countly.sqlite");
    cout << "opening " << fullpath << endl;
    if (sqlite3_open(fullpath.c_str(), &_sqlHandler) != SQLITE_OK)
    {
      cout << "Failed to setup sqlite" << endl;
      pthread_mutex_unlock(&_lock);
      return;
    }
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
        pthread_mutex_unlock(&_lock);
        return;
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
    pthread_mutex_unlock(&_lock);

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
      LoadDb();
    
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
      LoadDb();
    
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
      LoadDb();
    
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
    
    if (!_sqlHandler)
      LoadDb();
    
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

    if (!_sqlHandler)
      LoadDb();
    
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

