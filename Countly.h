//
//  Countly.h
//  CountlyCpp
//
//  Created by Noth on 26/10/14.
//  Copyright (c) 2014 Gith Security Systems. All rights reserved.
//

#ifndef __CountlyCpp__Countly__
#define __CountlyCpp__Countly__
#include "CountlyEventQueue.h"
#include "CountlyConnectionQueue.h"
#define COUNTLY_VERSION "0.6"


namespace CountlyCpp
{

  class Countly
  {
    public:
      static Countly * GetInstance() {if (!_instance) _instance = new Countly(); return _instance;}
    
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
