#ifndef COUNTLY_NATIVE_CRASH_BREAKPAD_HPP_
#define COUNTLY_NATIVE_CRASH_BREAKPAD_HPP_

#include <cstddef>
#include <string>

namespace cly {
namespace native_crash {
namespace breakpad {

/**
 * Turns on the Breakpad exception handler, which writes a minidump into the directory, with the memory block included,
 * and then hands the crash to the handler that was installed before it. The first call installs the handler and fixes
 * its directory for the life of the process. Returns false, with the reason in 'error', when the handler is on or the
 * directory is not the one of the first call.
 */
bool install(const std::string &absoluteDirectory, void *appMemory, size_t appMemorySize, std::string &error);

/**
 * Turns off the Breakpad exception handler: it stays installed for the life of the process and hands every crash to the
 * handler that was installed before it without a dump, so that no handler installed later is replaced.
 */
void uninstall();

/**
 * Whether the Breakpad exception handler is on.
 */
bool isInstalled();

} // namespace breakpad
} // namespace native_crash
} // namespace cly
#endif
