## XX.XX.XX
- ! Minor breaking change ! The "Consent required" option of the SDK Behavior Settings now applies. While it is on, the SDK collects nothing until the app gives consent; while it is off, the SDK works as if every consent were given, also in apps that require consent.
- ! Minor breaking change ! The "Automatic session tracking" option of the SDK Behavior Settings now applies. While it is off, the SDK tracks no session by itself and the app's session calls work, as with `enableManualSessionControl()`; while it is on, the SDK tracks sessions also in apps that called `enableManualSessionControl()`. A change while the SDK runs ends or begins the session.
- ! Minor breaking change ! Every request is now sent as POST, with its data in the request body. A client set with `setHTTPClient` gets `usePost` true for every request and must support POST. `alwaysUsePost` is deprecated and has no effect.
- ! Minor breaking change ! A client set with `setHTTPClient` must return the parsed JSON answer in `data` for every request: a request counts as delivered only when the answer is a JSON object with a `result`.
- ! Minor breaking change ! `start()` now refuses a server URL without `http://` or `https://`, without a host, with a port that is not a number from 1 to 65535, or with a query or a fragment, an empty app key and the placeholder `YOUR_APP_KEY`: it logs an error and the SDK stays off. Before, a URL without a scheme was used over plain HTTP.
- ! Minor breaking change ! The last parameter of `start()` has no effect anymore: with automatic sessions, the SDK always updates the session on the session update interval (60 seconds by default), and it moves recorded events into a request on every pass of its update loop (every 3 seconds by default, see `setUpdateInterval`). Before, `start()` without that parameter sent no session updates and held events until 100 were waiting. To send no session updates, call `enableManualSessionControl()`. `flushEvents()` moves the events into a request at once, and feedback answers (`[CLY]_nps`, `[CLY]_survey`, `[CLY]_star_rating`) and `[CLY]_push_action` events go into a request as soon as they are recorded.
- ! Minor breaking change ! `disableSDKBehaviorSettingsUpdates()` now stops every SDK Behavior Settings request, also the one at `start()`, so that only the settings given with `setSDKBehaviorSettings` or stored earlier apply. Before, it stopped only the periodic requests.
- ! Minor breaking change ! `beginSession`, `updateSession` and `endSession` now work only with `enableManualSessionControl()`; with automatic sessions they are ignored with a warning. A manual `updateSession()` now always reports the session duration, also when called sooner than the session update interval.
- ! Minor breaking change ! Remote config downloads now use the current remote config request and no longer enroll the user in A/B tests; call `enrollABOnRCDownload()` before `start()` to keep enrolling. With `enableRemoteConfig()`, values are downloaded at the end of `start()`, after a device ID change and when the consent is given, instead of at every session start.
- ! Minor breaking change ! Config values below their minimum are now ignored with a warning instead of being raised: 0 for the internal limits, `setMaxRequestQueueSize` and `setAutomaticSessionUpdateInterval`, and values below 1 for `setEventsToRQThreshold`. `setEventsToRQThreshold` also keeps values above 10000; the event queue still sends at 1000 events.
- ! Minor breaking change ! `setLocation` no longer uses values that don't fit their form, such as a lower case country code, coordinates that are not decimal degrees or an invalid IP address; it logs a warning for each.

- Added user consent. With `setRequiresConsent(true)` a feature works only after its consent is given, at start with `setConsentEnabled` or later with `giveConsent`, `giveConsentAll` or `setConsent`, and stops when the consent is removed with `removeConsent`, `removeConsentAll` or `setConsent`. The SDK sends every change to the server. The feature names are in `cly::consent`, and `cly::consent::allFeatures()` lists them all. `createFeatureGroup` and `setConsentFeatureGroup` handle groups of features; `getConsent` and `checkAllConsent` show the current state.
- While consent is required, changing the device ID without merge removes all consent, so the app gives it again for the new user.
- Added the health check feature. The SDK now sends a health check request once after `start()`. Like any request, it updates the user's last seen time on the server.
- Added the SDK name and version (`sdk_name` and `sdk_version`) to every request, including the remote config and SDK Behavior Settings fetches. Before, only the begin session request carried them.
- Added `multiplyCustomUserDetail` for multiplying the numeric value of a custom user property on the server.
- Added `changeServerUrl` to change the server URL after init.
- Added `changeAppKey` to change the app key after init.
- Added `flushQueues` to clear the request and event queues.
- Added `replaceAllAppKeysInQueueWithCurrentAppKey` to give the queued requests the current app key.
- Added `removeDifferentAppKeysFromQueue` to remove the queued requests of other app keys.
- Added `attemptToSendStoredRequests` to send the stored requests now.
- Added `disableHealthCheck()` for turning off the health check request.
- Added `status_code` and `body` to `HTTPResponse`. A custom HTTP client can fill them so that the health check reports the last request the server refused; a client that leaves them empty works as before.
- Calls made before `start()`, a second `start()`, beginning a session that already runs and ending one that does not are now logged at warning level. Failed and non-JSON server answers, and session calls ignored because the SDK Behavior Settings turned session tracking off, are now logged at debug level.
- Added device ID generation: when the app gives no device ID before `start()`, the SDK generates one, a new one at every start (see `setClearStoredDeviceID`). `getDeviceID()` and `getDeviceIDType()` return the device ID in use and where it came from. Every request carries the device ID type (`t`); the server counts users whose device ID the app gave as known users, so the dashboard's known user count can grow. After `start()`, `setDeviceID(id)` merges a generated device ID into the new one, as for a user who logs in, and changes a device ID the app gave without merge; `setDeviceID(id, same_user)` still decides explicitly.
- Added `setClearStoredDeviceID()`. By default `start()` uses the device ID given with `setDeviceID`, or a new generated one. With `setClearStoredDeviceID(false)`, SQLite builds keep the device ID between starts, and a stored device ID wins over one given before `start()`, which is ignored with a warning.
- Added the app version (`av`) and the number of waiting requests (`rr`) to every request.
- Added the operating system name and version (`_os`, `_os_version`) to the device metrics the SDK sends; values given with `setMetrics` still win. Apps that never called `setMetrics` now show their platform and operating system version on the dashboard.
- Added event IDs: every event carries its own ID and the current view (`cvid`), custom events the previous custom event (`peid`), view events the previous view (`pvid`) and the platform (`segment`), and crash reports the current view (`_cvid`).
- Added `setFailedRequestCooldown`: a failed request is tried again after this cool-down, 60 seconds by default, instead of on every update.
- Added `setRequestDropAgeHours` and the server's "dort" setting for dropping stored requests older than the given age.
- Added the backoff mechanism: when the server answers slowly, sending pauses for a while. `disableBackoffMechanism()` turns it off, and the SDK Behavior Settings can configure it. `stop()` doesn't wait for a pause: it sends the waiting requests before the SDK stops.
- Added gradual trimming of a request queue far over its size limit, at most 100 requests at a time; `disableGradualRequestCleaner()` drops the whole overflow at once.
- Added `setCrashFilterCallback` for changing or dropping crash reports before they are queued, and `setCustomCrashSegmentation` for segmentation added to every crash report.
- Added `crash().clearBreadcrumbs()`, `views().stopAllViews()`, and segmentation for `closeViewWithName` and `closeViewWithID`.
- Added `disableLocation()`. A location given with `setLocation` before `start()` is now used from `start()` on.

- Mitigated an issue where reaching the event queue threshold logged a warning that events were dropped, although they were sent.
- Mitigated an issue where a view that was open during a device ID change without merge was ended under the new device ID.
- Mitigated an issue where session starts let the server work out the user's location from the IP address while the SDK Behavior Settings turned location tracking off.
- Mitigated an issue where `start()` threw an exception when the app had set no device ID.
- Mitigated an issue where calling `views()` or `crash()` before `start()` crashed the app.
- Mitigated an issue where a log callback that threw an exception broke SDK calls or ended the app.
- Mitigated an issue where a segmentation whitelist or blacklist in the SDK Behavior Settings removed the names of views, so the views were missing from the dashboard.
- Mitigated an issue where, while the SDK Behavior Settings turned session tracking off, setting user details or changing the device ID blocked for 15 seconds and then deleted the waiting events, and a running session never ended.
- Mitigated an issue where any 2xx answer, such as a proxy's HTML page, counted as delivered, so the request was deleted.
- Mitigated an issue where a negative event count was sent as a very large number. Counts below 1 now become 1, and events with an empty key are ignored, with a warning.
- Mitigated an issue where an empty device ID was accepted and the server rejected every request.
- Mitigated an issue where `stop()` left the session open; it now sends the waiting events and ends the session before the SDK stops.
- Mitigated an issue where crash reports lacked the operating system and app version, so the server rejected them. A crash without a stack trace is now ignored with a warning.
- Mitigated an issue where breadcrumbs stayed after a crash and appeared in the next crash report.
- Mitigated an issue where an event timed with `startTimer` and `stopTimer` was recorded at its start instead of its end.
- Mitigated an issue where only the first view after `start()` was marked as the first view of a session, also without a running session.
- Mitigated an issue where custom view segmentation could replace the view's own keys, and `closeViewWithName` closed any one of several views with that name instead of the last one started.
- Mitigated an issue where a setting removed from the SDK Behavior Settings kept its last server value until the next start.
- Mitigated an issue where an empty blacklist in the SDK Behavior Settings switched off the matching whitelist.
- Mitigated an issue where events with a `[CLY]_` key the SDK does not know skipped the custom event switch, the event filters and the size limits.
- Mitigated an issue where `setSDKBehaviorSettings` ignored settings given as a copied server answer, and kept the given settings like a server answer for later starts.
- Mitigated an issue where the periodic settings fetch used the device ID from `start()`. The settings are now also fetched after a device ID change.
- Mitigated an issue where remote config values downloaded for the previous user stayed after a device ID change.
- Mitigated an issue where a separate location request carried the IP address.
- Mitigated an issue where values cut to the internal limits were not logged.
- Mitigated an issue where `setUserDetails` and `setCustomUserDetails` called before `start()` logged that the call was ignored, although the details are sent with the first session.
- Mitigated an issue where a C++ standard set with `CMAKE_CXX_STANDARD`, for example by a Conan profile, was ignored and the SDK was always compiled as C++14. C++14 stays the default and the minimum.
- Mitigated an issue where a server URL with a port or a path, such as `https://example.com:8443` or `https://example.com/countly`, was accepted but no request reached the server. A port in the URL now wins over the port argument, with a warning when they differ.
- Mitigated an issue where user details given before `start()` replaced each other, so that for example custom properties set with `setCustomUserDetails` were lost after a `setUserDetails` call.
- Mitigated an issue where events that an earlier start left in the SQLite database were sent under the device ID of the next start.

## 26.8.1
- ! Minor breaking change ! Builds now stop with a clear error when the SDK headers find a nlohmann/json older than 3.11 first on the include path, instead of mixing two versions of the library.

- Added the `COUNTLY_USE_SYSTEM_JSON` CMake option for building against the application's own nlohmann/json (3.11 or newer) instead of the bundled copy (default `OFF`).
- Added support for static library builds with the built-in HTTP client (libcurl) on Linux and macOS.
- Added CMake package files: an installed SDK is found with `find_package(countly CONFIG)` and linked as `countly::countly`, the same target name as with `add_subdirectory`.
- Added an in-memory fallback to SQLite builds: when no database path is set or the database cannot be opened, the SDK logs it and keeps events and requests in memory instead of not starting.
- Added `setDatabasePath` to builds without SQLite; it logs a warning and ignores the path, so the same code works with both builds.

- Mitigated an issue where session, view and timed event durations changed when the system clock was adjusted while the app was running.
- Mitigated an issue where `cmake --install` produced an include folder that could not be compiled against.
- Mitigated an issue where macOS builds replaced an `OPENSSL_ROOT_DIR` passed to CMake with Homebrew's OpenSSL.
- Mitigated an issue where Android NDK builds failed with `-Werror=format-security` errors in `constants.hpp`.
- Mitigated an issue where SQLite builds did not store events and requests that contained a single quote.
- Mitigated an issue where the previous user's details were sent with the new user's session after a device ID change without merge.
- Mitigated an issue where no requests were delivered when the server URL ended with a slash.
- Mitigated an issue where, on Linux and macOS, a server that stopped answering held back all further requests; requests now time out after 30 seconds.
- Mitigated an issue where apps using a SQLite-enabled SDK could crash unless they defined `COUNTLY_USE_SQLITE` themselves; the public headers no longer depend on it.

## 26.8.0
- ! Minor breaking change ! The SDK now enforces internal limits on recorded data (key length, value size, segmentation entry count, breadcrumb count, stack trace lines per thread and line length) across events, views, crashes, and user properties. Defaults can be overridden by server-side SDK Behavior Settings, or during init via `setMaxKeyLength`, `setMaxValueSize`, `setMaxSegmentationValues`, `setMaxBreadcrumbCount`, `setMaxStackTraceLinesPerThread` and `setMaxStackTraceLineLength`.
- ! Minor breaking change ! Updated the bundled nlohmann/json from v3.7.0 to v3.12.0. JSON types are part of the public API, so applications must be recompiled against the new headers; binaries built against the old headers will not link against a library built with the new ones.
- ! Minor breaking change ! SQLite is now bundled: when built with `COUNTLY_USE_SQLITE=ON`, the official SQLite amalgamation (3.53.4) is compiled into the library and no system SQLite is needed. This grows SQLite-enabled binaries by roughly 0.7 MB; set `COUNTLY_USE_SYSTEM_SQLITE=ON` to link the platform's SQLite instead. Existing database files remain fully compatible. After updating an existing clone, run `git submodule deinit -f vendor/sqlite` once.
- ! Minor breaking change ! When built with SQLite, each SDK instance requires its own database path. An instance claiming a path already in use logs an error and does not initialize.

- Added multi-instance support: several `Countly` instances can now run in one process, each with its own app key, queues, storage, and threads. Named instances can be managed with `createInstance`, `getInstance(name)`, `findInstance`, `hasInstance`, `destroyInstance` and `destroyAllInstances`.
- Added `shutdownNetworking()` for applications that load and unload the SDK without exiting the process.
- Added Windows support for the SQLite storage backend.
- Added a Software Bill of Materials: every release now ships a CycloneDX SBOM with a signed attestation; see SECURITY.md for details.

- Fixed events and requests being dropped on SQLite builds when database reads and writes from different threads overlapped.
- Fixed duplicate event and view IDs when many were generated in quick succession, most visibly on Windows.
- Fixed a possible crash when the SDK was destroyed while a remote config update was still in flight. Only one remote config fetch now runs at a time per instance; a call made while one is in flight is ignored.
- Fixed hangs and unbounded recursion when calling the SDK from inside the log callback; the event queue size getters now return -1 in that context instead of deadlocking.
- Fixed concurrent `endSession` calls each sending an `end_session` request.
- Fixed the event queue flush sending an empty request, and on SQLite builds failing with a database error, when another thread emptied the queue mid-flush.
- Fixed data races that could lose view events, report the wrong first view, or produce wrong `dow`, `hour` and `tz` values when recording from multiple threads.
- Fixed a storage error during initialization leaving the SDK's initialization state undefined.
- Fixed SDK shutdown potentially blocking for up to the full SDK Behavior Settings update interval.

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
