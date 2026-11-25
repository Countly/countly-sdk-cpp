#ifndef COUNTLY_NATIVE_CRASH_BREAKPAD_HPP_
#define COUNTLY_NATIVE_CRASH_BREAKPAD_HPP_

#include <cstddef>
#include <string>

namespace cly {
namespace native_crash {
namespace breakpad {

/**
 * Installs the Breakpad exception handler, which writes a minidump into the directory, with the memory block included,
 * and then hands the crash to the handler that was installed before it. Returns false when a handler is installed.
 */
bool install(const std::string &absoluteDirectory, void *appMemory, size_t appMemorySize);

/**
 * Removes the Breakpad exception handler and puts back the handlers that were installed before it.
 */
void uninstall();

/**
 * Whether the Breakpad exception handler is installed.
 */
bool isInstalled();

} // namespace breakpad
} // namespace native_crash
} // namespace cly
#endif
