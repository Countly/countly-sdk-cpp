//
//  CountlyConnectionQueue.cpp
//  CountlyCpp
//
//  Created by Noth on 26/10/14.
//  Copyright (c) 2014 Gith Security Systems. All rights reserved.
//

#include "CountlyConnectionQueue.h"
#include <stdio.h>
#include <sstream>
#include <iomanip>
#include "Countly.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

  //  https://count.ly/resources/reference/server-api
#define KEEPALIVE 30 * 1000 // Send keepalive every 30s
#define BUFFSIZE 512

using namespace std;
namespace CountlyCpp
{
  
  CountlyConnectionQueue::CountlyConnectionQueue():
  _beginSessionSent(false)
  {
    _version = COUNTLY_VERSION;
  }
  
  CountlyConnectionQueue::~CountlyConnectionQueue()
  {
    string URI = "/i?app_key=" + _appKey +"&device_id="+ _deviceId +"&end_session=1";
    HTTPGET(URI);
  }
  
  void CountlyConnectionQueue::SetAppKey(std::string key)
  {
    _appKey = key;
  }
  
  void CountlyConnectionQueue::SetAppHost(std::string host, int port)
  {
    _appHost = host;
    _appPort = port;
    
    if (host.find("http://") == 0)
    {
      _https = false;
      _appHostName = host.substr(7);
    }
    else if (_appHost.find("https://") == 0)
    {
      host = true;
      _appHostName = host.substr(8);
    }
    else
    {
      assert(0);
    }
      //Deal with http://bla.com/
    char p = _appHostName.find("/");
    if (p != string::npos)
      _appHostName = _appHostName.substr(0, p);
      //Resolve
    _appHost = ResolveHostname(_appHostName);

  }
 
  void CountlyConnectionQueue::BeginSession()
  {
    string URI = "/i?app_key=" + _appKey +"&device_id="+ _deviceId +"&sdk_version="+_version+"&begin_session=1";
    if (HTTPGET(URI))
      _beginSessionSent = true;
  }
  
  bool CountlyConnectionQueue::UpdateSession(CountlyEventQueue * queue)
  {
    int evtId;
    string URI;
    if (!_deviceId.size())
      _deviceId = queue->GetDeviceId();
    
    if (!_beginSessionSent)
      BeginSession();
    
    std::string json = queue->PopEvent(&evtId);
    if (evtId == -1)
    {
      if (Countly::GetTimestamp() - _lastSend > KEEPALIVE)
      {
        URI = "/i?app_key=" + _appKey +"&device_id="+ _deviceId +"&session_duration=30";
        HTTPGET(URI);
        cout << "KeepAlive sent" << endl;
      }
      return false;
    }
    
    cout << "Update " << evtId << endl;
    _lastSend = Countly::GetTimestamp();
  
    /*
     http://your_domain/i?
     app_key=AAA &
     device_id=BBB &
     events= [
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
    json = "[" + json + "]";
    URI = "/i?app_key=" + _appKey + "&device_id=" + _deviceId + "&events=" + URLEncode(json);
    cout << "URL : " << URI << endl;
    if (HTTPGET(URI))
    {
      queue->ClearEvent(evtId);
      cout << "Processed event " << evtId << endl;
    }
    return true;
  }
  
  string CountlyConnectionQueue::URLEncode(const string &value)
  {
    ostringstream escaped;
    escaped.fill('0');
    escaped << hex;
    
    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
      string::value_type c = (*i);
      
        // Keep alphanumeric and other accepted characters intact
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        escaped << c;
        continue;
      }
      
        // Any other characters are percent-encoded
      escaped << '%' << setw(2) << int((unsigned char) c);
    }
    
    return escaped.str();
  }
  
  bool CountlyConnectionQueue::HTTPGET(std::string URI)
  {
    stringstream http;
    bool ret;
    int s = Connect();
    char buf[512];
    if (s < 0) return false;
    
    http << "GET " << URI << " HTTP/1.0\r\n";
    http << "Host: "<<_appHostName << ":" "" << dec << _appPort << "\r\n";
    http << "Connection: Close\r\n";
    http << "User-Agent: Countly\r\n\r\n";
    
    ret = Send(s, (char *)http.str().c_str(), http.str().size());
    if (!ret)
    {
      close(s);
      return false;
    }

    memset(buf, 0x00, 512);
    int readSize = recv(s, (char *)buf, 512, 0);
    if ((readSize >= 15) && (!memcmp(buf, "HTTP/1.1 200 OK", 15)))
      ret = true;

    close(s);
    return ret;
  }
  
  
  std::string CountlyConnectionQueue::ResolveHostname(std::string hostname)
  {
    char result[32];
#ifdef WIN32
    InitWinsock();
#endif
    
    struct hostent * ent = gethostbyname(hostname.c_str());
    
    if (!ent)
      return "";
    
    if (!ent->h_addr_list || !ent->h_addr_list[0])
      return "";
    if (ent->h_length == 4)
    {
      unsigned char a, b, c, d;
      a = ent->h_addr_list[0][0];
      b = ent->h_addr_list[0][1];
      c = ent->h_addr_list[0][2];
      d = ent->h_addr_list[0][3];
      sprintf(result, "%u.%u.%u.%u",  (unsigned int) (a),
              (unsigned int) (b),
              (unsigned int) (c),
              (unsigned int) (d));
    }
    else if (ent->h_length == 8)
    {
      sprintf(result, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
              ent->h_addr_list[0][0],
              ent->h_addr_list[0][1],
              ent->h_addr_list[0][2],
              ent->h_addr_list[0][3],
              ent->h_addr_list[0][4],
              ent->h_addr_list[0][5],
              ent->h_addr_list[0][6],
              ent->h_addr_list[0][7]);
    }
    else
    {
      return "";
    }
    
    return std::string(result);
  }
  
  int CountlyConnectionQueue::Connect()
  {
    struct sockaddr_in remoteAddr;
    string host = ResolveHostname(_appHost);
    int s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    memset((char *) &remoteAddr, 0x00, sizeof(remoteAddr));
    remoteAddr.sin_family      = AF_INET;
    remoteAddr.sin_addr.s_addr = inet_addr((char *)host.c_str());
    remoteAddr.sin_port        = htons(_appPort);
    
    int  ret = connect(s,(struct sockaddr*)&remoteAddr,sizeof(remoteAddr));
#ifndef WIN32
    if (ret < 0)
    {
      switch (errno)
      {
        case EALREADY:
        case EINPROGRESS:
          return -1;
          break;
        case EISCONN: // Error status : "Is connected" ..
          break;
        default:
          return -1;
          break;
      }
    }
#else
    if (ret == SOCKET_ERROR)
    {
      switch (WSAGetLastError())
      {
        case WSAEALREADY:
        case WSAEINPROGRESS:
        case WSAEWOULDBLOCK:
        case WSAEINVAL: //according to Windock docs
          return -1;
        break;
        case WSAEISCONN:
        break;
        default:
          return -1;
        break;
      }
    }
#endif
    return s;
  }

  bool CountlyConnectionQueue::Send(int s, char * buffer, int size)
  {
    
    int ret = send(s, buffer, size, 0x00); //Send data
#ifndef WIN32
    if (ret < 0)
    {
      switch (errno)
      {
        case ENETDOWN:
        case ENETUNREACH:
          return false;
        break;
        case EINTR:
          return false;
        break;
        case ECONNRESET :
        case EHOSTUNREACH:
        case EPIPE:
        case ENOTCONN:
          return false;
        break;
        case EAGAIN: //Resource temporarily unavailable (retry later)
          return false; //FIXME
        break;
        case EACCES:
        case EBADF:
        case EFAULT:
        case EMSGSIZE:
        case ENOBUFS:
        case ENOTSOCK:
        default:
          return false;
        break;
      }
    }
#else
    if (ret == SOCKET_ERROR)
    {
      switch (WSAGetLastError())
      {
        case WSAEWOULDBLOCK: //not ready to write //Try again until timeout
          return false; //FIXME
        break;
        case WSAENETDOWN:
        case WSAENETUNREACH:
          return false;
        break;
        case WSAEINTR:
          return false;
        break;
        case WSAECONNRESET :
        case WSAEHOSTUNREACH:
        case WSAENOTCONN:
          return false;
        break;
        case WSAEACCES: //Broadcast addr
        case WSAEBADF: //
        case WSAEFAULT:
        case WSAEMSGSIZE: //Message too large
        case WSAENOBUFS:  //No buffer space
        case WSAENOTSOCK: //Not a socket
        default:
          return false;
        break;
      }
    }
#endif
    return true;
  }
}