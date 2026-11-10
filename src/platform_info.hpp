#ifndef COUNTLY_PLATFORM_INFO_HPP_
#define COUNTLY_PLATFORM_INFO_HPP_

#include <string>

namespace cly {
namespace platform {
/**
 * The operating system the SDK was built for, named as the server expects it in the "_os" metric, for example
 * "Windows", "macOS", "Linux" or "Android".
 */
std::string osName();

/**
 * The version of the running operating system without its name, for example "10.0.19045" on Windows or "14.4" on
 * macOS. On Linux and other Unix systems it is the kernel release. Empty when it can't be read.
 */
std::string osVersion();
} // namespace platform
} // namespace cly

#endif
