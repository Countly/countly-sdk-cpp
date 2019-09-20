## What's Countly?

[Countly](http://count.ly) is an innovative, real-time, open source mobile analytics and push notifications platform. It collects data from mobile devices, and visualizes this information to analyze mobile application usage and end-user behavior. There are two parts of Countly: [the server that collects and analyzes data](http://github.com/countly/countly-server), and mobile SDK that sends this data. Both parts are open source with different licensing terms.

* **Slack user?** [Join our Slack community](http://slack.count.ly:3000/)
* **Questions?** [Ask in our Community forum](http://community.count.ly)

## About this SDK

This repository contains the portable Countly C++ SDK. 

## Dependencies and building

Countly C++ SDK has been designed to work with very few dependencies in order to be available on most platforms.
In order to build this SDK, you need:

- a C++ compiler with C++11 support
- libcurl and its headers (with openssl) if you are on *nix
- cmake >= 3.0

First, clone the repository with its submodules:

``` shell
git clone --recursive https://github.com/Countly/countly-sdk-cpp
```

If you want to use SQLite to store session data persistently, build sqlite:

``` shell
# assuming we are on project root
cd vendor/sqlite
cmake -D BUILD_SHARED_LIBS -B build . # out of source build, we don't like clutter :)
# we define `BUILD_SHARED_LIBS` because sqlite's cmake file compiles statically by default for some reason
cd build
make # you might want to add something like -j8 to parallelize the build process
```

The cmake build flow is pretty straightforward:

``` shell
# assuming we are on project root again
ccmake -B build . # this will launch a TUI, configure the build as you see fit
cd build
make
```

## Usage

Typical use is:

```C++
#include "countly.hpp"

int main(int argc, char *argv[]) {
	Countly& ct = Countly::getInstance();
	// OS, OS_version, device, resolution, carrier, app_version);
	ct.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
	// Server and port
	ct.Start("abf2034f975393fa994d1cf8adf9a93e4a29ac29", "https://myserver.com", 403);
	ct.SetMaxEventsPerMessage(40);
	ct.SetMinUpdatePeriod(2000);

	ct.RecordEvent("MyCustomEvent", 123);
	ct.RecordEvent("MyCustomEvent", 17);
	ct.RecordEvent("MyCustomEvent", 34);
	ct.RecordEvent("AnotherCustomEvent", 644, 13.3);

	// Your program is supposed to do something..

	return 0;
}
```

## Testing

Build with the option `COUNTLY_BUILD_TESTS` on to build an executable that will run the tests:

``` shell
cmake -D COUNTLY_BUILD_TESTS -B build . # or do it interactively with ccmake
cd build
make
./countly-tests
```

