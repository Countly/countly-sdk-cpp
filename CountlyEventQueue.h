/*
 CountlyEventQueue.h
 CountlyCpp
 
 Created by Benoit Girard on 26/10/14.
 
 The MIT License (MIT)
 
 Copyright (c) 2015 Kontrol SAS (tanker.io)
 
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


#ifndef COUNTLYEVENTQUEUE_H_
#define COUNTLYEVENTQUEUE_H_

#include <deque>
#include <iostream>
#include <map>
#include <string>

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifndef NOSQLITE
#include "./vendor/sqlite3/sqlite3.h"
#endif

#include "./vendor/arduino-json/ArduinoJson.h"

using std::string;

namespace CountlyCpp {

class CountlyEventQueue {
 public:
    CountlyEventQueue();
    ~CountlyEventQueue();

    void SetPath(string path);

    int Count();
    string PopEvent(int* evtId, size_t offset);
    size_t PopEvents(int* eventIds, string* events,
                     size_t capacity, size_t offset);
    void ClearEvent(int evtId);

    bool RecordEvent(string key, int count);
    bool RecordEvent(string key, int count, double sum);
    bool RecordEvent(string key,
      std::map<string, string> segmentation, int count);
    bool RecordEvent(string key,
      std::map<string, string> segmentation, int count, double sum);

    string GetDeviceId();

 private:
    bool AddEvent(string json);
    bool LoadDb();
    void Lock();
    void Unlock();
    string MakeDeviceId();

    string           _path;

#ifndef NOSQLITE
    sqlite3*         _sqlHandler;
#else
    struct EventsItem {
      int            evtId;
      string         json;
    };
    std::deque<EventsItem>  _events;
    int              _evtIdCounter;
    string           _deviceid;
#endif

#ifndef _WIN32
    pthread_mutex_t  _lock;
#else
    HANDLE           _lock;
#endif
};

}  // namespace CountlyCpp

#endif  // COUNTLYEVENTQUEUE_H_
