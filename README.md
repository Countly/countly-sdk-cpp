CountlyCpp
==========

C++ SDK for Countly (count.ly)

CountlyCpp is a portable SDK for Countly (http://count.ly) written in C++.

**Dependencies**

CountlyCpp has been designed to work with very few deps in order to be portable on most platforms.

**Building**

* clone this repository
* `git submodule init`
* `git submodule update`
* launch `generate.cmd`

Windows: `MSBuild.exe CountlyCpp.sln` or run MSVS against CountlyCpp.sln

Linux: `make`

**Usage**

Typical use is:

```C++
#include "Countly.h"

using namespace CountlyCpp;

int main(int argc, char * argv[])
{
  Countly * ct = Countly::GetInstance();
  ct->SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Free", "1.0");
  ct->Start("abf2034f975393fa994d1cf8adf9a93e4a29ac29", "http://myserver.com", 8080);
  
  ct->RecordEvent("MyCustomEvent", 123);
  ct->RecordEvent("MyCustomEvent", 17);
  ct->RecordEvent("MyCustomEvent", 34);
  ct->RecordEvent("AnotherCustomEvent", 644, 13.3);
 
  sleep(4); // Your program is supposed to do something..
  
  delete(ct);
  return 0;
}
```

**Testing**

`npm install --ignore-scripts`
`npm test`
