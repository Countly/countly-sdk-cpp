21.11.1
* !! Major breaking change !! Fixed a bug that triggered when providing segmentation to the "RecordEvent" call. Previously, by mistake, every segmentation value was parsed as a JSON and threw an exception when it wasn't a valid JSON string. Now this will not be the case and every String value can be provided. This is marked as a "major breaking change" in case some integrations were adding workarounds to this issue.
* ! Minor breaking change ! Default automatic session update duration changed to 60 seconds.
* Added a call to change the automatic session update duration.
* Fixed a bug where session duration was reported wrong.
* Fixed a bug that caused GET requests to fail on Linux.
* Fixed bug when changing device id with server merge.
* Fixed bug when device id was changed without server merge. Previously the new session was started with the old device ID and not the new one.
* Fixed a bug that was a typo ('COUNTLY_CUSTOM_HTTP' instead of 'COUNTLY_USE_CUSTOM_HTTP') in the cmake file that cause the SDK to be misconfigured. 
* Fixed a bug that caused POST requests to fail on Windows.

21.11.0
* Fixed session duration issue.
* Added functionality to report event duration manually.
* 'startOnCloud' in 'Countly' is deprecated and this is going to be removed in the future.
