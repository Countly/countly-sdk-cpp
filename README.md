CountlyCpp
==========

C++ SDK for Countly (count.ly)


CountlyCpp is a portable SDK for Countly (http://count.ly) written in C++.


**Dependencies:**

CountlyCpp has been designed to work with very few deps in order to be portable on most platforms.
This SDK will require :
* pthread (which is available on most platforms)
That's all (sqlite, HTTP, etc. is embedded) !

**Limitations :**
* Only support http mode (no https yet)
* Dirty HTTP implementation. Will only deal with typical cases.

**Usage :**

Typical use is :

```C++
#include "Countly.h"

using namespace CountlyCpp;

int main(int argc, char * argv[])
{
  Countly * ct = Countly::GetInstance();
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
