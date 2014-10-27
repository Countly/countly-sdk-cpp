//
//  CountlyConnectionQueue.h
//  CountlyCpp
//
//  Created by Noth on 26/10/14.
//  Copyright (c) 2014 Gith Security Systems. All rights reserved.
//

#ifndef __CountlyCpp__CountlyConnectionQueue__
#define __CountlyCpp__CountlyConnectionQueue__
#include <iostream>
#include "CountlyEventQueue.h"

namespace CountlyCpp
{
  
  class CountlyConnectionQueue
  {
  public:
    CountlyConnectionQueue();
    ~CountlyConnectionQueue();
    
    void SetAppKey(std::string key);
    void SetAppHost(std::string host, int port);
    bool UpdateSession(CountlyEventQueue * queue);

  private:
    std::string _appKey;
    std::string _appHost;
    std::string _appHostName;
    int         _appPort;
    std::string _deviceId;
    std::string _version;
    unsigned long long _lastSend;
    bool        _beginSessionSent;
    bool        _https;
    
    void BeginSession();
    std::string URLEncode(const std::string &value);
    bool HTTPGET(std::string URI);
    std::string ResolveHostname(std::string hostname);
    int Connect();
    bool Send(int s, char * buffer, int size);
  };
}
#endif /* defined(__CountlyCpp__CountlyConnectionQueue__) */
