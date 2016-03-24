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

#ifndef __CountlyCpp__CountlyEventQueue__
#define __CountlyCpp__CountlyEventQueue__
#include <iostream>
#include <map>
#include <deque>

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

#ifndef NOSQLITE
#include "sqlite3.h"
#endif

namespace CountlyCpp
{
  
  class CountlyEventQueue
  {
    public:
      CountlyEventQueue();
      ~CountlyEventQueue();
    
      void SetPath(std::string path);

      int Count();
      std::string PopEvent(int * evtId, size_t offset);
      void ClearEvent(int evtId);
    
      bool RecordEvent(std::string key, int count);
      bool RecordEvent(std::string key, int count, double sum);
      bool RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count);
      bool RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count, double sum);
   
      std::string GetDeviceId();


    private:
      bool AddEvent(std::string json);
      bool LoadDb();
      void Lock();
      void Unlock();
      std::string MakeDeviceId();

      std::string       _path;
    
#ifndef NOSQLITE
      sqlite3 *          _sqlHandler;
#else
      struct EventsItem {
        int                     evtId;
        std::string             json;
      };
      std::deque<EventsItem>    _events;
      int                       _evtIdCounter;
      std::string               _deviceid;
#endif

#ifndef _WIN32
      pthread_mutex_t   _lock;
#else
      HANDLE            _lock;
#endif

  };
  
}
#endif /* defined(__CountlyCpp__CountlyEventQueue__) */
