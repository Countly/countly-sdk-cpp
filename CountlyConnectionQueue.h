/*
  CountlyConnectionQueue.h
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


#ifndef COUNTLYCONNECTIONQUEUE_H_
#define COUNTLYCONNECTIONQUEUE_H_

#include <stdint.h>
#include <iostream>
#include <string>
#include "CountlyEventQueue.h"

namespace CountlyCpp {

class CountlyConnectionQueue {
 public:
    CountlyConnectionQueue();
    ~CountlyConnectionQueue();

    void SetAppKey(std::string key);
    void SetAppHost(std::string host, int port);
    void SetMaxEventsPerMessage(int maxEvents);
    void SetMetrics(std::string os, std::string os_version, std::string device,
      std::string resolution, std::string carrier, std::string app_version);

    bool UpdateSession(CountlyEventQueue * queue);

 private:
    std::string  _appKey;
    std::string  _appHost;
    std::string  _appHostName;
    int          _appPort;
    int          _maxEvents;
    std::string  _deviceId;
    std::string  _version;
    uint64_t     _lastSend;
    bool         _beginSessionSent;
    bool         _https;

    std::string  _os;
    std::string  _os_version;
    std::string  _device;
    std::string  _resolution;
    std::string  _carrier;
    std::string  _app_version;

    bool BeginSession();
    std::string URLEncode(const std::string &value);
    bool HTTPGET(std::string URI);
};

}  // namespace CountlyCpp

#endif  // COUNTLYCONNECTIONQUEUE_H_
