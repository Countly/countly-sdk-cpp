#ifndef COUNTLY_NATIVE_CRASH_HPP_
#define COUNTLY_NATIVE_CRASH_HPP_

#include <cstddef>
#include <string>
#include <vector>

namespace cly {
namespace native_crash {

/**
 * The largest dump the SDK sends, in bytes. Base64 and URL encoding make the request about 1.42 times larger, and the
 * server refuses POST fields over 20 MB, so a larger dump would block the request queue.
 */
constexpr unsigned long long MAX_DUMP_BYTES = 10ULL * 1024 * 1024;

#ifdef _WIN32
/**
 * Converts UTF-8 text to UTF-16, for the wide Windows file APIs.
 */
std::wstring widen(const std::string &text);
#endif

/**
 * The paths of the files in the directory whose name ends with ".dmp" in any case, sorted by name. Empty when the
 * directory can't be read.
 */
std::vector<std::string> listDumpFiles(const std::string &directory);

/**
 * Whether the path is an existing directory.
 */
bool isDirectory(const std::string &path);

/**
 * The size of a file in bytes, -1 when the file can't be opened.
 */
long long fileSize(const std::string &path);

/**
 * Reads a whole file into 'content'. Returns false when the file can't be read.
 */
bool readFile(const std::string &path, std::string &content);

/**
 * Deletes a file. Returns false when the file can't be deleted.
 */
bool removeFile(const std::string &path);

/**
 * A dump file that this process holds alone while it reads and deletes it, so that another process that reads the same
 * directory neither sends it a second time nor deletes it meanwhile. Closing the claim releases it.
 */
class DumpClaim {
public:
  DumpClaim() = default;
  ~DumpClaim();
  DumpClaim(const DumpClaim &) = delete;
  DumpClaim &operator=(const DumpClaim &) = delete;

  /**
   * What claim() found: CLAIMED when this process holds the dump now, TAKEN when another process holds it or has
   * deleted it since the directory was listed, NOT_OPENED when the path is a link or no regular file, or the dump can't
   * be opened.
   */
  enum class Result { CLAIMED, TAKEN, NOT_OPENED };

  /**
   * Opens the dump and claims it.
   */
  Result claim(const std::string &path);

  /**
   * The size of the claimed dump in bytes, -1 when it can't be read.
   */
  long long size();

  /**
   * Reads the whole claimed dump into 'content'. Returns false when it can't be read.
   */
  bool read(std::string &content);

  /**
   * Deletes the claimed dump and releases the claim. Returns false when the dump can't be deleted.
   */
  bool remove();

private:
  /**
   * Releases the claim, if any.
   */
  void release();
#ifdef _WIN32
  void *handle = nullptr;
#else
  int descriptor = -1;
#endif
  std::string claimedPath;
};

/**
 * Encodes bytes as Base64 with the standard alphabet and padding.
 */
std::string base64Encode(const std::string &bytes);

/**
 * The largest payload a context block holds, in bytes.
 */
size_t maxContextPayload();

/**
 * The bytes of a context block that holds the payload, laid out as the crash handler keeps it in memory. Empty when the
 * payload is larger than maxContextPayload().
 */
std::string encodeContextBlock(const std::string &payload);

/**
 * Finds an intact context block in the bytes of a dump and gives its payload. Returns false when the dump has none.
 */
bool findContextPayload(const std::string &dump, std::string &payload);

/**
 * Replaces the payload of the context block that every dump of the crash handler contains. Returns false, and leaves
 * the block empty, when the payload is larger than maxContextPayload().
 */
bool setContextPayload(const std::string &payload);

/**
 * A copy of the bytes of the context block, as every dump of the crash handler holds them.
 */
std::string contextBlock();

/**
 * Whether this build has a crash handler, which needs COUNTLY_USE_BREAKPAD.
 */
bool isHandlerAvailable();

/**
 * Turns on the process-wide crash handler, which writes a minidump into the directory when the process crashes and then
 * hands the crash to the handler that was installed before it. The first call fixes the directory for the life of the
 * process. Returns false, with the reason in 'error', when the build has no crash handler, the directory does not exist
 * or is not the one of the first call, or the handler is already on.
 */
bool installHandler(const std::string &directory, std::string &error);

/**
 * Turns off the crash handler. It stays installed and hands every crash on without a dump, so that a handler that other
 * code installed later is never replaced.
 */
void uninstallHandler();

/**
 * Whether the crash handler is on.
 */
bool isHandlerInstalled();

} // namespace native_crash
} // namespace cly
#endif
