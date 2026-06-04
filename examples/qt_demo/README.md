# Countly C++ SDK Demo App

A Qt6 desktop application for manually testing all features of the Countly C++ SDK, including SDK Behavior Settings (SBS) and a standalone Feedback Widgets flow.

## Prerequisites

| Requirement | Notes |
| ----------- | ----- |
| **CMake** | 3.16 or newer |
| **A C++17 compiler** | AppleClang / Clang / GCC are all fine |
| **Qt 6** | `Widgets` + `WebEngineWidgets` modules are both required |
| **libcurl** | Used by the Feedback Widgets tab for direct HTTP calls |
| **Countly C++ SDK** | Built locally with `-DCOUNTLY_USE_SQLITE=ON` |

### Installing the dependencies on macOS

Homebrew's Qt ships as ~40 keg-only sub-packages (`qtbase`, `qtwebengine`, ...). Qt's own CMake config expects them in a single prefix, so install the unified `qt` formula — it symlinks all the sub-kegs into `/opt/homebrew/opt/qt` where `find_package(Qt6)` can discover every component:

```bash
brew install cmake qt curl
```

`brew install qt@6` or installing `qtbase`/`qtwebengine` individually is **not enough** — `find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)` will fail because the per-keg prefixes don't contain each other's config files. If you already have the sub-packages installed, running `brew install qt` is quick; it only adds the symlink farm on top.

### Installing the dependencies on Linux

On Debian/Ubuntu:

```bash
sudo apt install cmake build-essential libcurl4-openssl-dev \
                 qt6-base-dev qt6-webengine-dev
```

On Fedora/RHEL:

```bash
sudo dnf install cmake gcc-c++ libcurl-devel \
                 qt6-qtbase-devel qt6-qtwebengine-devel
```

## Setup

### 1. Build the Countly C++ SDK

This demo lives inside the SDK repo at `examples/qt_demo/`. It links against `libcountly.dylib` (macOS) / `libcountly.so` (Linux) produced by the SDK's own build, so build the SDK first with SQLite support:

```bash
cd /path/to/countly-sdk-cpp           # the repo root, two levels above this README
mkdir -p build && cd build
cmake .. -DCOUNTLY_USE_SQLITE=ON
make
```

After this, `countly-sdk-cpp/build/` will contain the Countly shared library. The demo's `CMakeLists.txt` discovers the SDK automatically via `${CMAKE_CURRENT_SOURCE_DIR}/../..` — no path flag needed.

### 2. Create `dev_config.hpp`

The "Load Dev Config" button in the Init tab pulls credentials from this header so you don't have to retype them every run. It's in `.gitignore` — never commit real credentials.

Create `examples/qt_demo/dev_config.hpp` with:

```cpp
#ifndef DEV_CONFIG_HPP
#define DEV_CONFIG_HPP

#include <string>

struct DevConfig {
    std::string serverUrl  = "https://your.server.ly";
    std::string appKey     = "YOUR_APP_KEY";
    std::string deviceId   = "test-device-id";
    std::string dbPath     = "countly_demo.db";
    std::string port       = "";
    std::string salt       = "";
    std::string eqThreshold = "";
    std::string rqMaxSize  = "";
    std::string rqBatchSize = "";
    std::string sessionInterval = "";
    std::string updateInterval  = "";
    std::string metricsOs          = "macOS";
    std::string metricsOsVersion   = "15.0";
    std::string metricsDevice      = "MacBook";
    std::string metricsResolution  = "2560x1600";
    std::string metricsCarrier     = "";
    std::string metricsAppVersion  = "1.0";
    std::string sbsJson    = "";
    bool manualSession     = false;
    bool disableSBSUpdates = false;
    bool alwaysPost        = false;
    bool enableRemoteConfig = false;
    bool disableAutoEventsOnUP = false;
};

#endif
```

### 3. Configure and build the demo

From the `examples/qt_demo/` folder:

```bash
mkdir -p build && cd build

cmake \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  ..

make -j$(sysctl -n hw.ncpu)    # on Linux: -j$(nproc)
```

One flag worth explaining:

- **`CMAKE_PREFIX_PATH`** tells CMake where Qt lives. On macOS with Homebrew this is `/opt/homebrew/opt/qt` (Apple Silicon) or `/usr/local/opt/qt` (Intel). On Linux with distro packages you can usually omit it entirely — the pkg-config files are already on the default search path.

If you keep the SDK checkout somewhere other than this demo's grandparent directory (e.g. a separate working tree), pass `-DCOUNTLY_SDK_DIR=/path/to/countly-sdk-cpp` to override the default.

The binary is written to `build/qt_demo`.

### 4. Run

```bash
./qt_demo
```

In the app:

1. Go to **Init / Config**, click **Load Dev Config** (or fill the fields manually), then **Initialize SDK**.
2. Switch tabs to exercise the feature you want to test. The SDK's log output streams into the panel at the bottom in real time.
3. For feedback widgets, head to the **Feedback Widgets** tab and click **Fetch Widgets** — it reuses the Init tab's server URL / app key / device ID. Click any widget card to present it in the embedded web view.

## Tabs

The app has 10 tabs covering all SDK features:

| Tab | Description |
| --- | ----------- |
| **Init / Config** | Server URL, app key, device ID, metrics, SDK options (manual sessions, SBS JSON, salt, etc.). Initialize and stop the SDK from here. |
| **Sessions** | Begin, update, and end sessions manually. |
| **Events** | Send basic events, events with count/sum/duration, and events with custom segmentation. |
| **Views** | Start and stop named views. |
| **Crashes** | Record handled exceptions, add breadcrumbs, set custom crash segments. |
| **User Profile** | Set standard user properties (name, email, etc.) and custom key-value pairs. |
| **Location** | Set country code, city, GPS coordinates, or IP address. Disable location. |
| **Device ID** | Change device ID with or without server merge. |
| **Remote Config** | Fetch all remote config values or fetch specific keys. View the returned JSON. |
| **Feedback Widgets** | Standalone HTTP flow (does not use the SDK). Fetches feedback widgets from the server and renders them in an embedded `QWebEngineView`, intercepting Countly widget communication URLs. Uses the Server URL / App Key / Device ID from the Init tab. See `Countly_Feedback_Widget_Implementation_Guide.html` for the underlying protocol. |

## Log Panel

A live log panel at the bottom of the window shows all SDK log output in real time. Logs are delivered via a thread-safe Qt signal/slot connection so background SDK threads can log without crashing the UI.

## Troubleshooting

- **`Could NOT find Qt6WebEngineWidgets (missing: Qt6WebEngineWidgets_DIR)`**
  You're pointing CMake at a Qt prefix that only contains `qtbase`. Install the unified Homebrew `qt` formula (`brew install qt`) or add the `qtwebengine` prefix alongside in `CMAKE_PREFIX_PATH`.

- **Build fails after editing `CMakeLists.txt` or moving the SDK**
  Delete the `build/` folder and reconfigure. CMake caches `find_package` results; adding a new dependency or changing paths requires a fresh configure, not an incremental rebuild.

- **`libcountly.dylib` not found at launch**
  The build embeds `${COUNTLY_SDK_DIR}/build` as an `LC_RPATH`. Moving the SDK directory after building invalidates that path. Reconfigure (or patch with `install_name_tool -add_rpath`).

- **WebEngine window is blank / never loads**
  On macOS the WebEngine process needs network permissions; make sure your firewall isn't blocking `QtWebEngineProcess`. You can also watch the log panel — the demo logs every navigation interception the page makes.

## Notes

- `dev_config.hpp` is in `.gitignore` — never commit credentials.
- The SDK is linked as a dynamic library. Rebuild the SDK first whenever you update it, then rebuild the demo.
- The Feedback Widgets tab intentionally bypasses the C++ SDK — it talks to `/o/sdk?method=feedback` and `/feedback/<type>` directly via libcurl. This mirrors the manual protocol documented in `Countly_Feedback_Widget_Implementation_Guide.html` and is useful for validating the server-side widget setup independently of the SDK.
- On exit the app calls `Countly::getInstance().stop()` before the main window is destroyed so background SDK threads don't call back into a dead GUI.
