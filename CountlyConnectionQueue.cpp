/*
 CountlyConnectionQueue.cpp
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

#include "CountlyConnectionQueue.h"
#include <stdio.h>
#include <sstream>
#include <iomanip>
#include "Countly.h"
#include <stdio.h>
#include <string.h>

#ifdef WIN32
  #include <winsock2.h>
#else
  #include <sys/socket.h>
  #include <fcntl.h>
  #include <netinet/tcp.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

#ifndef NOSSL
  #include <openssl/bio.h>
  #include <openssl/err.h>
  #include <openssl/pem.h>
  #include <openssl/x509.h>
  #include <openssl/x509_vfy.h>
#endif


#include <sys/types.h>
#include <stdlib.h>

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
  _lastSend(0),
  _beginSessionSent(false)
  {
    _version = COUNTLY_VERSION;

#ifdef WIN32
    WSADATA winit;

    if (WSAStartup(MAKEWORD(2,2),&winit) != 0)
    {

    }
#endif
#ifndef NOSSL
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
#endif
  }
  
  CountlyConnectionQueue::~CountlyConnectionQueue()
  {
    string URI = "/i?app_key=" + _appKey +"&device_id="+ _deviceId +"&end_session=1";
    HTTPGET(URI);

#ifdef WIN32
    WSACleanup();
#endif
#ifndef NOSSL
    ERR_free_strings();
    EVP_cleanup();
#endif
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
#ifdef NOSSL
      assert(0);
#endif
      _https = true;
      _appHostName = host.substr(8);
    }
    else
    {
      assert(0);
    }
      //Deal with http://bla.com/
    size_t p = _appHostName.find("/");
    if (p != string::npos)
      _appHostName = _appHostName.substr(0, p);
      //Resolve
    _appHost = ResolveHostname(_appHostName);

  }
 
  void CountlyConnectionQueue::SetMetrics(std::string os, std::string os_version, std::string device, std::string resolution, std::string carrier, std::string app_version)
  {
    _os           = os;
    _os_version   = os_version;
    _device       = device;
    _resolution   = resolution;
    _carrier      = carrier;
    _app_version  = app_version;
  }
  
  void CountlyConnectionQueue::BeginSession()
  {
    string URI = "/i?app_key=" + _appKey +"&device_id="+ _deviceId +"&sdk_version="+_version+"&begin_session=1";
    bool metricsOk = false;
    std::string metrics = "{\n";
    
    /*
     metrics={ "_os": "Android", "_os_version": "4.1", "_device": "Samsung Galaxy", "_resolution": "1200x800", "_carrier": "Vodafone", "_app_version": "1.2", "_density": "200dpi" }
     
     http://your_domain/i?
     app_key=AAA &
     device_id=BBB &
     sdk_version=CCC &
     begin_session=1 &
     metrics= {
     "_os": "DDD",
     "_os_version": "EEE",
     "_device": "FFF",
     "_resolution": "GGG",
     "_carrier": "HHH",
     "_app_version": "III"
     }

     */
    
    if (_os.size())
    {
      metrics += "\"_os\":\"" + _os + "\"";
      metricsOk = true;
    }
    if (_os_version.size())
    {
      if (metricsOk) metrics += ",\n";
      metrics += "\"_os_version\":\"" + _os_version + "\"";
      metricsOk = true;
    }
    if (_device.size())
    {
      if (metricsOk) metrics += ",\n";
      metrics += "\"_device\":\"" + _device + "\"";
      metricsOk = true;
    }
    if (_resolution.size())
    {
      if (metricsOk) metrics += ",\n";
      metrics += "\"_resolution\":\"" + _resolution + "\"";
      metricsOk = true;
    }
    if (_carrier.size())
    {
      if (metricsOk) metrics += ",\n";
      metrics += "\"_carrier\":\"" + _carrier + "\"";
      metricsOk = true;
    }
    if (_app_version.size())
    {
      if (metricsOk) metrics += ",\n";
      metrics += "\"_app_version\":\"" + _app_version + "\"";
      metricsOk = true;
    }
    metrics += "\n}";
    
    if (metricsOk)
      URI += "&metrics=" + URLEncode(metrics);
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
        _lastSend = Countly::GetTimestamp();
      }
      return false;
    }
    
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
    if (HTTPGET(URI))
    {
      queue->ClearEvent(evtId);
      return true;
    }
    else
    {
      return false;
    }
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
    int s;
    char buf[512];
    int readSize;

    if (_https)
      s = ConnectSSL();
    else
      s = Connect();
    if (s < 0)
      return false;
    

    http << "GET " << URI << " HTTP/1.0\r\n";
    http << "Host: "<<_appHostName << ":" "" << dec << _appPort << "\r\n";
    http << "Connection: Close\r\n";
    http << "User-Agent: Countly " << Countly::GetVersion() << "\r\n\r\n";
    
    ret = Send(s, (char *)http.str().c_str(), http.str().size());
    if (!ret)
    {
      close(s);
      return false;
    }

    memset(buf, 0x00, 512);
    if(_https)
      readSize = SSL_read(_sslHandler, (char *)buf, 512);
    else
      readSize = recv(s, (char *)buf, 512, 0);
    
    if ((readSize >= 15) && (!memcmp(buf, "HTTP/1.1 200 OK", 15)))
      ret = true;
    close(s);
    return ret;
  }
  
  
  std::string CountlyConnectionQueue::ResolveHostname(std::string hostname)
  {
    char result[32];
    
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
  
  void CountlyConnectionQueue::Close(int s)
  {
    close(s);
  }
  
#ifndef NOSSL
  int CountlyConnectionQueue::ConnectSSL()
  {
    int s;
    SSL_CTX * sslContext;
    int err;
    
    s = Connect();
    if (s < 0)
      return s;
    
    sslContext = SSL_CTX_new( SSLv23_client_method());
    _sslHandler = SSL_new(sslContext);
    SSL_set_fd(_sslHandler, s);
    
    err = SSL_connect(_sslHandler);
    if (err <= 0)
    {
      switch (SSL_get_error(_sslHandler, err))
      {
        case SSL_ERROR_NONE:
          break;
        case SSL_ERROR_ZERO_RETURN:
          break;
        case SSL_ERROR_WANT_READ:
          break;
        case SSL_ERROR_WANT_WRITE:
          break;
        case SSL_ERROR_WANT_CONNECT:
          break;
        case SSL_ERROR_WANT_ACCEPT:
          break;
        case SSL_ERROR_WANT_X509_LOOKUP:
          break;
        case SSL_ERROR_SYSCALL:
          break;
        case SSL_ERROR_SSL:
          break;
      }
      SSL_shutdown(_sslHandler);
      SSL_free(_sslHandler);
      return -1;
    }
    return s;
  }
  
  void CountlyConnectionQueue::CloseSSL(int s)
  {
    SSL_shutdown(_sslHandler);
    SSL_free(_sslHandler);
    Close(s);
  }
#endif
  
  
  
  bool CountlyConnectionQueue::Send(int s, char * buffer, int size)
  {
    int ret;

    if (_https)
      ret = SSL_write(_sslHandler, buffer, size);
    else
      ret = send(s, buffer, size, 0x00); //Send data

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
