/*
 Countly.h
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


#ifndef __CountlyCpp__Countly__
#define __CountlyCpp__Countly__

#define COUNTLY_VERSION "0.6"

#include <map>
#include <iostream>
#include <pthread.h>

namespace CountlyCpp
{
  class CountlyEventQueue;
  class CountlyConnectionQueue;
  
  class Countly
  {
    public:
      static Countly * GetInstance();
      static void DeleteInstance();
      std::string GetVersion();

      void SetPath(std::string path);  //Setup work directory (where Countly sqlite file will be written)
      void SetMetrics(std::string os, std::string os_version, std::string device, std::string resolution, std::string carrier, std::string app_version);
      void Start(std::string appKey, std::string host, int port);
      void StartOnCloud(std::string appKey);
      void Stop();
    
      void RecordEvent(std::string key, int count);
      void RecordEvent(std::string key, int count, double sum);
      void RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count);
      void RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count, double sum);

      //Internal
      void StartThreadTimer();
    
      static unsigned long long GetTimestamp();

    private:
      static Countly      * _instance;
      CountlyEventQueue   * _eventQueue;
      CountlyConnectionQueue   * _connectionQueue;

      pthread_t             _thread;
      bool                  _threadRunning;

    
      Countly();
      ~Countly();
      void TimerUpdate();
    
  
  
  };

}

#endif /* defined(__CountlyCpp__Countly__) */
