/*
 Countly.h
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


#ifndef COUNTLY_H_
#define COUNTLY_H_

#define COUNTLY_VERSION "16.02"

#include <stdint.h>
#include <map>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

using std::string;

namespace CountlyCpp {

class CountlyEventQueue;
class CountlyConnectionQueue;

class Countly {
 public:
    static Countly* GetInstance();
    static void DeleteInstance();
    static string GetVersion();

    // setup work directory (where countly.deviceid is)
    void SetPath(string path);
    void SetMetrics(string os, string os_version, string device,
      string resolution, string carrier, string app_version);
    void SetMaxEventsPerMessage(int maxEvents);
    void SetMinUpdatePeriod(int minUpdateMillis);
    void Start(string appKey, string host, int port);
    void StartOnCloud(string appKey);
    void Stop();

    void RecordEvent(string key, int count);
    void RecordEvent(string key, int count, double sum);
    void RecordEvent(string key,
      std::map<string, string> segmentation, int count);
    void RecordEvent(string key,
      std::map<string, string> segmentation, int count, double sum);

    // internal
    void StartThreadTimer();
    static uint64_t GetTimestamp();

 private:
    static Countly*          _instance;
    CountlyEventQueue*       _eventQueue;
    CountlyConnectionQueue*  _connectionQueue;
    int                      _minUpdateMillis;

#ifndef _WIN32
    pthread_t                _thread;
#else
    HANDLE                   _thread;
#endif

    bool                     _threadRunning;


    Countly();
    ~Countly();
    void TimerUpdate();
};

}  // namespace CountlyCpp

#endif  // COUNTLY_H_
