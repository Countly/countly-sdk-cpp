21.11.1
* ! Minor breaking change ! Default automatic session update duration changed to 60 seconds.
* Added a call to change the automatic session update duration.
* Fixed a bug where session duration was reported wrong.
* Fixed bug when changing device id with server merge.
* Fixed bug when device id was changed without server merge. Previously the new session was started with the old device ID and not the new one.

21.11.0
* Fixed session duration issue.
* Added functionality to report event duration manually.
* 'startOnCloud' in 'Countly' is deprecated and this is going to be removed in the future.
