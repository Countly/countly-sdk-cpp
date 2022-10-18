# Countly C++ SDK

![travis build status](https://img.shields.io/travis/Countly/countly-sdk-cpp?style=flat-square)

This repository contains the portable Countly C++ SDK. 

## What's Countly?

[Countly](http://count.ly) is an innovative, real-time, open source mobile analytics and push notifications platform. It collects data from mobile devices, and visualizes this information to analyze mobile application usage and end-user behavior.
There are two parts of Countly: [the server that collects and analyzes data](http://github.com/countly/countly-server), and mobile SDK that sends this data. Both parts are open source with different licensing terms.

* **Slack user?** [Join our Slack community](http://slack.count.ly:3000/)
* **Questions?** [Ask in our Community forum](http://community.count.ly)

## About

This repository includes the Countly C++ SDK.

Need help? See [Countly SDK for C++](https://support.count.ly/hc/en-us/articles/4416163384857-C-) documentation at [Countly Resources](http://resources.count.ly), or ask us on our [Countly Analytics Community Slack channel](http://slack.count.ly).

## Security

Security is very important to us. If you discover any issue regarding security, please disclose the information responsibly by sending an email to security@count.ly and **not by creating a GitHub issue**.

## Dependencies and building

Countly C++ SDK has been designed to work with very few dependencies in order to be available on most platforms.
In order to build this SDK, you need:

* a C++ compiler with C++14 support
* libcurl (with openssl) and its headers if you are on *nix
* cmake >= 3.13

First, clone the repository with its submodules:

``` shell
git clone --recursive https://github.com/Countly/countly-sdk-cpp
```

If you want to use SQLite to store session data persistently, build sqlite:

``` shell
# assuming we are on project root
cd vendor/sqlite
cmake -D BUILD_SHARED_LIBS=1 -B build . # out of source build, we don't like clutter :)
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
using namespace cly;

int main(int argc, char *argv[]) {
	Countly& ct = Countly::getInstance();
	// OS, OS_version, device, resolution, carrier, app_version);
	ct.SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Carrier", "1.0");
	ct.setCustomUserDetails({{"Account Type", "Basic"}, {"Employer", "Company4"}});
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
cmake -D COUNTLY_BUILD_TESTS=1 -B build . # or do it interactively with ccmake
cd build
make
./countly-tests
```

To run unit tests associated with 'SQLITE' and 'Custom SHA-256' build executable with the options 
`COUNTLY_USE_SQLITE` and `COUNTLY_BUILD_TESTS`:

``` shell
cmake -DCOUNTLY_BUILD_TESTS=1 -DCOUNTLY_USE_SQLITE=1 -DCOUNTLY_USE_CUSTOM_SHA256=1 -B build
```

## Other Github resources

This SDK needs one of the following Countly Editions to work:

* Countly Community Edition, [downloadable from Github](https://github.com/Countly/countly-server)
* [Countly Enterprise Edition](http://count.ly/product)

For more information about Countly Enterprise Edition, see [comparison of different Countly editions](https://count.ly/compare/)

There are also other Countly SDK repositories (both official and community supported) on [Countly resources](http://resources.count.ly/v1.0/docs/downloading-sdks).

## How can I help you with your efforts?
Glad you asked. We need ideas, feedback and constructive comments. All your suggestions will be taken care with utmost importance. We are on [Twitter](http://twitter.com/gocountly) and [Facebook](http://www.facebook.com/Countly) if you would like to keep up with our fast progress!

## Badges

If you like Countly, [why not use one of our badges](https://count.ly/brand-assets) and give a link back to us, so others know about this wonderful platform?

<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/dark.svg?v2" alt="Countly - Product Analytics" /></a>

```JS
<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/dark.svg" alt="Countly - Product Analytics" /></a>
```

<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/light.svg?v2" alt="Countly - Product Analytics" /></a>

```JS
<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/light.svg" alt="Countly - Product Analytics" /></a>
```

### Support

For Community support, visit [http://community.count.ly](http://community.count.ly "Countly Community Forum").
