CountlyCpp
==========

C++ SDK for Countly (count.ly)


CountlyCpp is a portable SDK for Countly (http://count.ly) written in C++.

**Dependencies:**

CountlyCpp has been designed to work with very few deps in order to be portable on most platforms.
This SDK will require :
* pthread (which is available on most platforms)

That's all (sqlite, HTTP, JSON, etc. are embedded) !

**Limitations :**
* Dirty HTTP implementation. Will only deal with typical cases.

**Building**
Build SDK using provided Makefiles : make -f Makefile.xxx
Include CountlyCpp.h in your project.

Link with -lCountlyCpp -lssl -lcrypto
If you dont plan to use HTTPS, link with -lCountlyCpp -DNOSSL


**Usage :**

Typical use is :

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

**Versions**
1.0 :  First release
1.1 :  SetPath is now mandatory before use
1.2 : Linux compatibility
1.3 : Cleaning up things (was really too Quick and too Dirty.)
1.4 : Added HTTPS support
