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

#include <assert.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <WinHTTP.h>
#else
#include <curl/curl.h>
#endif

#include "Countly.h"

//  https://count.ly/resources/reference/server-api
#define KEEPALIVE 30  // send keepalive every 30s
#define BUFFSIZE 512

using std::string;

namespace CountlyCpp {

CountlyConnectionQueue::CountlyConnectionQueue() {
  _maxEvents = 50;
  _version = COUNTLY_VERSION;
  _lastSend = 0;
  _beginSessionSent = false;
#ifndef _WIN32
  curl_global_init(CURL_GLOBAL_ALL);
#endif
}

CountlyConnectionQueue::~CountlyConnectionQueue() {
  if (_beginSessionSent) {
    string URI = "/i?app_key=" + _appKey + "&device_id=" +
      _deviceId + "&end_session=1";
    HTTPGET(URI);
  }
#ifndef _WIN32
  curl_global_cleanup();
#endif
}

void CountlyConnectionQueue::SetAppKey(string key) {
  _appKey = key;
}

void CountlyConnectionQueue::SetAppHost(string host, int port) {
  _appHost = host;
  _appPort = port;

  if (host.find("http://") == 0) {
    _https = false;
    _appHostName = host.substr(7);
  } else if (_appHost.find("https://") == 0) {
    _https = true;
    _appHostName = host.substr(8);
  } else {
    assert(0);
  }

  // deal with http://bla.com/
  size_t p = _appHostName.find("/");
  if (p != string::npos)
    _appHostName = _appHostName.substr(0, p);
}

void CountlyConnectionQueue::SetMaxEventsPerMessage(int maxEvents) {
  _maxEvents = maxEvents;
}

void CountlyConnectionQueue::SetMetrics(string os, string os_version,
  string device, string resolution, string carrier, string app_version) {
  _os          = os;
  _os_version  = os_version;
  _device      = device;
  _resolution  = resolution;
  _carrier     = carrier;
  _app_version = app_version;
}

bool CountlyConnectionQueue::BeginSession() {
  string URI = "/i?app_key=" + _appKey + "&device_id=" +
    _deviceId + "&sdk_version=" + _version + "&begin_session=1";
  bool metricsOk = false;
  string metrics = "{\n";

  if (_os.size()) {
    metrics += "\"_os\":\"" + _os + "\"";
    metricsOk = true;
  }
  if (_os_version.size()) {
    if (metricsOk) metrics += ",\n";
    metrics += "\"_os_version\":\"" + _os_version + "\"";
    metricsOk = true;
  }
  if (_device.size()) {
    if (metricsOk) metrics += ",\n";
    metrics += "\"_device\":\"" + _device + "\"";
    metricsOk = true;
  }
  if (_resolution.size()) {
    if (metricsOk) metrics += ",\n";
    metrics += "\"_resolution\":\"" + _resolution + "\"";
    metricsOk = true;
  }
  if (_carrier.size()) {
    if (metricsOk) metrics += ",\n";
    metrics += "\"_carrier\":\"" + _carrier + "\"";
    metricsOk = true;
  }
  if (_app_version.size()) {
    if (metricsOk) metrics += ",\n";
    metrics += "\"_app_version\":\"" + _app_version + "\"";
    metricsOk = true;
  }
  metrics += "\n}";

  if (metricsOk)
    URI += "&metrics=" + URLEncode(metrics);

  if (!HTTPGET(URI)) return false;
  _lastSend = Countly::GetTimestamp();
  return true;
}

// returns true only if no more events to send
bool CountlyConnectionQueue::UpdateSession(CountlyEventQueue* queue) {
  int evtId;
  std::vector<int> evtIds;
  string all;
  string URI;

  if (!_deviceId.size())
    _deviceId = queue->GetDeviceId();

  if (!_beginSessionSent) {
    if (!BeginSession()) return false;
    _beginSessionSent = true;
  }

  string json;

  for (int i = 0; i < _maxEvents; i++) {
    json = queue->PopEvent(&evtId, i);
    if (evtId == -1) break;
    evtIds.push_back(evtId);
    if (i > 0) all = all + ",";
    all = all + json;
  }

  if (evtIds.size() == 0) {
    if (Countly::GetTimestamp() - _lastSend > KEEPALIVE * 1000) {
      std::ostringstream URI;
      URI << "/i?app_key=" + _appKey + "&device_id=" +
        _deviceId + "&session_duration=";
      URI << KEEPALIVE;
      if (!HTTPGET(URI.str())) return false;
      _lastSend = Countly::GetTimestamp();
    }
    return true;  // true is only here. no events
    // and successful keepalive (if needed)
  }

  all = "[" + all + "]";
  URI = "/i?app_key=" + _appKey + "&device_id=" +
    _deviceId + "&events=" + URLEncode(all);
  if (!HTTPGET(URI)) return false;
  _lastSend = Countly::GetTimestamp();
  for (size_t i = 0; i < evtIds.size(); i++)
    queue->ClearEvent(evtIds[i]);
  return false;  // false! see above
}

string CountlyConnectionQueue::URLEncode(const string &value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
    string::value_type c = (*i);

    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << '%' << std::setw(2) << int((unsigned char) c);
  }

  return escaped.str();
}

bool CountlyConnectionQueue::HTTPGET(string URI) {
  bool ok = false;

  // printf("%s\n", URI.c_str());

#ifndef _WIN32
  CURL* curl;
  CURLcode code;
  curl = curl_easy_init();
  if (curl) {
    std::stringstream fullURI;
    fullURI << (_https ? "https://" : "http://");
    fullURI << _appHostName << ":" << _appPort << URI;
    curl_easy_setopt(curl, CURLOPT_URL, fullURI.str().c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
      ok = (code == 200);
    }
    curl_easy_cleanup(curl);
  }
#else
  HINTERNET hSession;
  HINTERNET hConnect;
  HINTERNET hRequest;

  hSession = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (hSession) {
    wchar_t wideHostName[256];
    MultiByteToWideChar(0, 0, _appHostName.c_str(), -1, wideHostName, 256);
    hConnect = WinHttpConnect(hSession, wideHostName, _appPort, 0);
  }
  if (hConnect) {
    wchar_t wideURI[65536];
    MultiByteToWideChar(0, 0, URI.c_str(), -1, wideURI, 65536);
    hRequest = WinHttpOpenRequest(hConnect, L"GET", wideURI, NULL,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      _https ? WINHTTP_FLAG_SECURE : 0);
  }
  if (hRequest) {
    std::stringstream headers;
    headers << "User-Agent: Countly " << Countly::GetVersion();
    wchar_t wideHeaders[256];
    MultiByteToWideChar(0, 0, headers.str().c_str(), -1, wideHeaders, 256);
    ok = WinHttpSendRequest(hRequest, wideHeaders, headers.str().size(),
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0;
    if (ok) {
      ok = WinHttpReceiveResponse(hRequest, NULL) != 0;
      if (ok) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE |
          WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
          &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        ok = dwStatusCode == 200;
      }
    }
    WinHttpCloseHandle(hRequest);
  }
  if (hConnect) {
    WinHttpCloseHandle(hConnect);
  }
  if (hSession) {
    WinHttpCloseHandle(hSession);
  }
#endif

  return ok;
}

}  // namespace CountlyCpp
