## Build Scripts

Scripts provided here offers a convenient way to build sample app and the tests at different platforms.
Still SQLite setup must be done separately at the "/vendor/sqlite" directory if SQLite is needed.
Current cmake options set in the script that differs from the default are:

- COUNTLY_BUILD_SAMPLE = ON
- COUNTLY_BUILD_TESTS = ON

To change any other options you can manually modify the scripts.

### Run

You can run the scripts simply by executing them from the root directory.

### Flow

When run the script would:

- Check if a build folder is present at the root and erase it if it exists
- Run Cmake with mentioned options
- Build countly-sample app
- Build countly-test

After this point you can just go to the "/build" directory and execute those build files
