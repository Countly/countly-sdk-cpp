//
//  CountlyEventQueue.h
//  CountlyCpp
//
//  Created by Noth on 26/10/14.
//  Copyright (c) 2014 Gith Security Systems. All rights reserved.
//

#ifndef __CountlyCpp__CountlyEventQueue__
#define __CountlyCpp__CountlyEventQueue__
#include <iostream>
#include <map>

#include "sqlite3.h"

namespace CountlyCpp
{
  
  class CountlyEventQueue
  {
    public:
      CountlyEventQueue();
      ~CountlyEventQueue();
      int Count();
      std::string PopEvent(int * evtId);
      void ClearEvent(int evtId);
    
      void RecordEvent(std::string key, int count);
      void RecordEvent(std::string key, int count, double sum);
      void RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count);
      void RecordEvent(std::string key, std::map<std::string, std::string> segmentation, int count, double sum);
   
      std::string GetDeviceId();


    private:
      void AddEvent(std::string json);
      sqlite3 *     _sqlHandler;
      pthread_mutex_t   _lock;

    
  };
  
}
#endif /* defined(__CountlyCpp__CountlyEventQueue__) */
