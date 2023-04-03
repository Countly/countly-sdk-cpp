[![Codacy Badge](https://app.codacy.com/project/badge/Grade/f3268a85b0034b68aa4fc47c9dce596c)](https://www.codacy.com/gh/Countly/countly-sdk-cpp/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=Countly/countly-sdk-cpp&amp;utm_campaign=Badge_Grade)

# Countly C++ SDK

This repository contains the Countly C++ SDK, which can be integrated into C++ applications. The Countly C++ SDK is intended to be used with [Countly Community Edition](https://github.com/Countly/countly-server) or [Countly Enterprise Edition](https://count.ly/product).

## What is Countly?
[Countly](https://count.ly) is a product analytics solution and innovation enabler that helps teams track product performance and customer journey and behavior across [mobile](https://count.ly/mobile-analytics), [web](http://count.ly/web-analytics),
and [desktop](https://count.ly/desktop-analytics) applications. [Ensuring privacy by design](https://count.ly/privacy-by-design), Countly allows you to innovate and enhance your products to provide personalized and customized customer experiences, and meet key business and revenue goals.

Track, measure, and take action - all without leaving Countly.

* **Questions or feature requests?** [Join the Countly Community on Discord](https://discord.gg/countly)
* **Looking for the Countly Server?** [Countly Community Edition repository](https://github.com/Countly/countly-server)
* **Looking for other Countly SDKs?** [An overview of all Countly SDKs for mobile, web and desktop](https://support.count.ly/hc/en-us/articles/360037236571-Downloading-and-Installing-SDKs#officially-supported-sdks)

## Integrating Countly SDK in your projects

For a detailed description on how to use this SDK [check out our documentation](https://support.count.ly/hc/en-us/articles/4416163384857-C-).

For information about how to add the SDK to your project, please check [this section of the documentation](https://support.count.ly/hc/en-us/articles/4416163384857-C-#adding-the-sdk-to-the-project).

You can find minimal SDK integration information for your project in [this section of the documentation](https://support.count.ly/hc/en-us/articles/4416163384857-C-#minimal-setup).

For an example integration of this SDK, you can have a look [here](https://github.com/Countly/countly-sdk-cpp/tree/master/examples).

This SDK supports the following features:
* [Analytics](https://support.count.ly/hc/en-us/articles/4431589003545-Analytics)
* [User Profiles](https://support.count.ly/hc/en-us/articles/4403281285913-User-Profiles)
* [A/B Testing](https://support.count.ly/hc/en-us/articles/4416496362393-A-B-Testing-)

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

## Security
Security is very important to us. If you discover any issue regarding security, please disclose the information responsibly by sending an email to security@count.ly and **not by creating a GitHub issue**.

## Badges
If you like Countly, [why not use one of our badges](https://count.ly/brand-assets) and give a link back to us so others know about this wonderful platform?

<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/dark.svg?v2" alt="Countly - Product Analytics" /></a>

```JS
<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/dark.svg" alt="Countly - Product Analytics" /></a>
```

<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/light.svg?v2" alt="Countly - Product Analytics" /></a>

```JS
<a href="https://count.ly/f/badge" rel="nofollow"><img style="width:145px;height:60px" src="https://count.ly/badges/light.svg" alt="Countly - Product Analytics" /></a>
```

## How can I help you with your efforts?
Glad you asked! For community support, feature requests, and engaging with the Countly Community, please join us at [our Discord Server](https://discord.gg/countly). We're excited to have you there!

Also, we are on [Twitter](https://twitter.com/gocountly) and [LinkedIn](https://www.linkedin.com/company/countly) if you would like to keep up with Countly related updates.
