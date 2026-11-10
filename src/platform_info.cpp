#include "platform_info.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <sys/system_properties.h>
#else
#include <sys/utsname.h>
#endif

namespace cly {
namespace platform {
std::string osName() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
#if defined(TARGET_OS_VISION) && TARGET_OS_VISION
  return "visionOS";
#elif TARGET_OS_WATCH
  return "watchOS";
#elif TARGET_OS_TV
  return "tvOS";
#elif TARGET_OS_IPHONE
  return "iOS";
#else
  return "macOS";
#endif
#elif defined(__ANDROID__)
  return "Android";
#elif defined(__linux__)
  return "Linux";
#elif defined(__unix__)
  return "Unix";
#else
  return "Unknown";
#endif
}

std::string osVersion() {
#if defined(_WIN32)
  // GetVersionEx reports the version the app is manifested for; RtlGetVersion reports the real one
  typedef LONG(WINAPI * RtlGetVersionFunction)(PRTL_OSVERSIONINFOW);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == nullptr) {
    return std::string();
  }
  RtlGetVersionFunction getVersion = reinterpret_cast<RtlGetVersionFunction>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")));
  if (getVersion == nullptr) {
    return std::string();
  }
  RTL_OSVERSIONINFOW info = {};
  info.dwOSVersionInfoSize = sizeof(info);
  if (getVersion(&info) != 0) {
    return std::string();
  }
  return std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
#elif defined(__APPLE__)
  char version[64] = {0};
  size_t size = sizeof(version);
  if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0) {
    return std::string();
  }
  return std::string(version);
#elif defined(__ANDROID__)
  char version[PROP_VALUE_MAX] = {0};
  if (__system_property_get("ro.build.version.release", version) <= 0) {
    return std::string();
  }
  return std::string(version);
#else
  struct utsname name;
  if (uname(&name) != 0) {
    return std::string();
  }
  return std::string(name.release);
#endif
}
} // namespace platform
} // namespace cly
