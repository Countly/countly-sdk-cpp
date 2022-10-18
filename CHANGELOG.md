22.06.1
* !! Major breaking change !! We are removing the 'LogLevel' enum out of the 'Countly' class which will change how that enum can be referenced. 'Countly::LogLevel' will not work, you will have to use 'cly::LogLevel' instead.
* Added functionality to record crash.
* Added ability to record breadcrumbs for crash recording.

22.06.0
* !! Major breaking change !! We are adding the 'cly' namespace on 'Countly' class which will change how that class can be referenced. 'Countly::' will not work, you will have to use 'cly::Countly::' instead.
* !! Major breaking change !! We are extracting the 'Event' class out of the 'Countly' class which will change how that class can be referenced. 'Countly::Event' will not work, you will have to use 'cly::Event' instead.
* !! Major breaking change !! Increased the compiler version required to compile the SDK. It's increased from version C++11 to C++14.
* Making network requests has been reworked. They will now be sent on a separate thread. Requests will also be added in an internal queue and will be sent one at a time.
* When making network requests, the SDK will now unlock it's mutex.
* Fixed a bug that caused an exception on windows when encoding data that contains special characters. 

22.02.0
* Added 10-second time-outs for all windows HTTP transactions.
* Added ability to record views.

21.11.3
* Added functionality to set custom SHA256.

21.11.2
* Fixed a bug that occurred after trying to erase events from the SQLite database when there were none.
* Fixed a bug with the checksum calculation.

21.11.1
* !! Major breaking change !! Fixed a bug that triggered when providing segmentation to the "RecordEvent" call. Previously, by mistake, every segmentation value was parsed as a JSON and threw an exception when it wasn't a valid JSON string. 
Now this will not be the case and every String value can be provided. This is marked as a "major breaking change" in case some integrations were adding workarounds to this issue.
* ! Minor breaking change ! Default automatic session update duration changed to 60 seconds.
* Added a call to change the automatic session update duration.
* Fixed a bug where session duration was reported wrong.
* Fixed a bug that caused an exception when the application quit. This was due to the SDK attempting to send an end session request.
* Fixed an issue with the custom HTTP client function pointer by setting it's default value.
* Fixed a bug that caused GET requests to fail on Linux.
* Fixed bug when changing device id with server merge.
* Fixed bug when device id was changed without server merge. Previously the new session was started with the old device ID and not the new one.
* Fixed a bug that was a typo ('COUNTLY_CUSTOM_HTTP' instead of 'COUNTLY_USE_CUSTOM_HTTP') in the cmake file that cause the SDK to be misconfigured. 
* Fixed a bug that caused POST requests to fail on Windows.
* Fixed issues with location requests.
* Deprecated old location calls and introduced a new location call

21.11.0
* Fixed session duration issue.
* Added functionality to report event duration manually.
* 'startOnCloud' in 'Countly' is deprecated and this is going to be removed in the future.
