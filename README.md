## What's Countly?

[Countly](http://count.ly) is an innovative, real-time, open source mobile analytics and push notifications platform. It collects data from mobile devices, and visualizes this information to analyze mobile application usage and end-user behavior. There are two parts of Countly: [the server that collects and analyzes data](http://github.com/countly/countly-server), and mobile SDK that sends this data. Both parts are open source with different licensing terms.

* **Slack user?** [Join our Slack community](http://slack.count.ly:3000/)
* **Questions?** [Ask in our Community forum](http://community.count.ly)

## About this SDK

This repository includes the portable Countly C++ SDK. 


## Dependencies and building

Countly C++ SDK has been designed to work with very few deps in order to be portable on most platforms.

In order to build this SDK, Python 2.7 is required. Package `libcurl4-openssl-dev` is also required in Linux.

First, clone the repository and run following commands:

```
`git submodule update --init --recursive'
launch `generate` or `generate.cmd`
```

On Windows, run `MSBuild.exe CountlyCpp.sln` or open CountlyCpp.sln in MSVS

On Linux, run `make`

## Usage

Typical use is:

```C++
#include "Countly.h"

using namespace CountlyCpp;

int main(int argc, char * argv[])
{
  Countly* ct = Countly::GetInstance();
  // OS, OS_version, device, resolution, carrier, app_version);
  ct->SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
  // Server and port
  ct->Start("abf2034f975393fa994d1cf8adf9a93e4a29ac29", "https://myserver.com", 403);
  ct->SetMaxEventsPerMessage(40);
  ct->SetMinUpdatePeriod(2000);
  
  ct->RecordEvent("MyCustomEvent", 123);
  ct->RecordEvent("MyCustomEvent", 17);
  ct->RecordEvent("MyCustomEvent", 34);
  ct->RecordEvent("AnotherCustomEvent", 644, 13.3);
 
  sleep(4); // Your program is supposed to do something..
  
  delete(ct);
  return 0;
}
```

## Testing

* `npm install --ignore-scripts`
* `npm test` or `COUNTLY_VALGRIND=1 npm test`
