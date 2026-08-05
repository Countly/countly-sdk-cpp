## X.X.X
- ! Minor breaking change ! Added SDK internal limits enforcement (max key length, value size, segmentation values, breadcrumb count, stack-trace lines per thread, stack-trace line length) across events, views, crashes, and user properties. Limits use config defaults overridable by server SDK Behavior Settings, and can be set via `setMaxKeyLength`, `setMaxValueSize`, `setMaxSegmentationValues`, `setMaxBreadcrumbCount`, `setMaxStackTraceLinesPerThread`, `setMaxStackTraceLineLength` during init.

- Added multi-instance support: several `Countly` instances can now run in one process, each with its own app key, queues, storage, and threads. Instances can be owned by the integrator, or created and looked up by name with `createInstance`, `getInstance(name)`, `findInstance`, `hasInstance`, `destroyInstance` and `destroyAllInstances`.
- ! Minor breaking change ! When built with SQLite, each instance requires its own database path. A second instance claiming a path already in use logs an error and does not initialize.
- Fixed the libcurl global lifecycle: `curl_global_init` now runs once per process, and cleanup no longer runs when an instance is destroyed, which could tear down networking underneath another live instance. Added `shutdownNetworking()` for hosts that load and unload the SDK without exiting.
- Fixed non-unique event and view IDs: the random component of generated IDs was constant for the lifetime of the process, and on platforms with a coarse `system_clock` (Windows, ~15ms) the timestamp component did not change either, so IDs generated within one tick were identical.
- Remote config fetches now run on an owned thread that is joined when the SDK is destroyed, instead of being detached, which could leave a fetch running after the objects it used were gone. Only one fetch runs at a time per instance: a call made while a fetch is in flight is logged and ignored rather than queued, so `updateRemoteConfig`, `updateRemoteConfigFor` and `updateRemoteConfigExcept` never block the calling thread.
- Fixed a lost wakeup when stopping the periodic SDK Behavior Settings timer: the stop flag was set without holding the mutex the timer thread waits on, so the notification could be missed and the joining thread could block for up to the full four-hour update interval.

## 26.1.1
- Updated CMake minimum required version to use the range format with upper the end of `3.31`.
- Hardened mutex handling against exceptions.

## 26.1.0
- ! Minor breaking change ! SDK Behavior Settings is now enabled by default. Changes made on SDK Manager > SDK Behavior Settings on your server will affect SDK behavior directly.

- Added `enableImmediateRequestOnStop` configuration option. When enabled, the update loop uses a condition variable instead of polling, allowing `stop()` and `setUpdateInterval()` to take effect immediately rather than waiting for the current sleep interval to expire.
- Added init config method "disableSDKBehaviorSettingsUpdates" to disable periodic SBS updates from the server.
- Added init config method "setSDKBehaviorSettings" to provide server configuration in JSON format during initialization.

- Fixed OpenSSL discovery in CMakeLists.txt to dynamically resolve the Homebrew prefix, supporting both Apple Silicon and Intel Macs.
- Added `dow` (day of week) and `hour` fields to every event.
- Added `dow`, `hour`, and `tz` (timezone offset in minutes) fields to every request.

## 23.2.4
- Mitigated an issue where cached events were not queued when a user property was recorded.

## 23.2.3
- Mitigated an issue where the new device ID was used when ending a session if device ID was changed without merging.

## 23.2.2
- Mitigated a mutex issue that can happen during update loop.

## 23.2.1
- Added manual session control via "Countly::enableManualSessionControl". When enabled, automatic session calls are ignored, while manual calls remain usable for finer control.
- Added "checkRQSize" function to return the current number of requests in the queue.

## 23.2.0
- Request queue processing now is limited to 100 requests at a time
- Added 'setEventsToRQThreshold' method that sets the number of events after which all events will be sent to the RQ. Default value is set to 100.
- Mitigated an issue where not providing a virtual port number (or providing a negative value) at the 'start' was causing SDK to assign a wrong port number.

## 22.09.1
- Mitigated a problem that caused invalid pointer error if the database path was set wrong 

## 22.09.0
- ! Minor breaking change ! SDK configuration now can't be changed after initialization/start
- Added a persistent requests queue when building the SDK with the 'COUNTLY_USE_SQLITE' flag
- Fixed a bug where view's name was being overridden by segmentation provided.

## 22.06.4
- Fixed a bug where the SDK 'mutex' was being locked twice when built with the 'COUNTLY_USE_SQLITE' flag.

## 22.06.3
- Fixed a bug where empty metrics were sent with session begin request.

## 22.06.2
- Fixed a bug where metrics were not sent with session begin request.

## 22.06.1
- !! Major breaking change !! We are removing the 'LogLevel' enum out of the 'Countly' class which will change how that enum can be referenced. 'Countly::LogLevel' will not work, you will have to use 'cly::LogLevel' instead.
- Added functionality to record crash.
- Added ability to record breadcrumbs for crash recording.

## 22.06.0
- !! Major breaking change !! We are adding the 'cly' namespace on 'Countly' class which will change how that class can be referenced. 'Countly::' will not work, you will have to use 'cly::Countly::' instead.
- !! Major breaking change !! We are extracting the 'Event' class out of the 'Countly' class which will change how that class can be referenced. 'Countly::Event' will not work, you will have to use 'cly::Event' instead.
- !! Major breaking change !! Increased the compiler version required to compile the SDK. It's increased from version C++11 to C++14.
- Making network requests has been reworked. They will now be sent on a separate thread. Requests will also be added in an internal queue and will be sent one at a time.
- When making network requests, the SDK will now unlock it's mutex.
- Fixed a bug that caused an exception on windows when encoding data that contains special characters. 

## 22.02.0
- Added 10-second time-outs for all windows HTTP transactions.
- Added ability to record views.

## 21.11.3
- Added functionality to set custom SHA256.

## 21.11.2
- Fixed a bug that occurred after trying to erase events from the SQLite database when there were none.
- Fixed a bug with the checksum calculation.

## 21.11.1
- !! Major breaking change !! Fixed a bug that triggered when providing segmentation to the "RecordEvent" call. Previously, by mistake, every segmentation value was parsed as a JSON and threw an exception when it wasn't a valid JSON string. 
Now this will not be the case and every String value can be provided. This is marked as a "major breaking change" in case some integrations were adding workarounds to this issue.
- ! Minor breaking change ! Default automatic session update duration changed to 60 seconds.
- Added a call to change the automatic session update duration.
- Fixed a bug where session duration was reported wrong.
- Fixed a bug that caused an exception when the application quit. This was due to the SDK attempting to send an end session request.
- Fixed an issue with the custom HTTP client function pointer by setting it's default value.
- Fixed a bug that caused GET requests to fail on Linux.
- Fixed bug when changing device id with server merge.
- Fixed bug when device id was changed without server merge. Previously the new session was started with the old device ID and not the new one.
- Fixed a bug that was a typo ('COUNTLY_CUSTOM_HTTP' instead of 'COUNTLY_USE_CUSTOM_HTTP') in the cmake file that cause the SDK to be misconfigured. 
- Fixed a bug that caused POST requests to fail on Windows.
- Fixed issues with location requests.
- Deprecated old location calls and introduced a new location call

## 21.11.0
- Fixed session duration issue.
- Added functionality to report event duration manually.
- 'startOnCloud' in 'Countly' is deprecated and this is going to be removed in the future.
