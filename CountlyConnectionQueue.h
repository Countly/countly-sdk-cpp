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
    void SetMetrics(std::string os, std::string os_version, std::string device, std::string resolution, std::string carrier, std::string app_version);

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
    
    std::string _os;
    std::string _os_version;
    std::string _device;
    std::string _resolution;
    std::string _carrier;
    std::string _app_version;

    
    void BeginSession();
    std::string URLEncode(const std::string &value);
    bool HTTPGET(std::string URI);
    std::string ResolveHostname(std::string hostname);
    int Connect();
    bool Send(int s, char * buffer, int size);
  };
}
#endif /* defined(__CountlyCpp__CountlyConnectionQueue__) */
