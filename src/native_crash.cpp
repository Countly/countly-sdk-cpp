#include "native_crash.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef COUNTLY_USE_BREAKPAD
#include "native_crash_breakpad.hpp"
#endif

namespace cly {
namespace native_crash {
namespace {

constexpr size_t CONTEXT_BLOCK_SIZE = 64 * 1024;
constexpr size_t CONTEXT_HEADER_SIZE = 24;
constexpr char CONTEXT_MAGIC[16] = {'C', 'L', 'Y', '_', 'N', 'A', 'T', 'I', 'V', 'E', '_', 'C', 'T', 'X', '_', '1'};

// Registered with the crash handler, so every dump it writes contains these bytes: the magic, the payload length and
// checksum as little-endian 32-bit numbers, then the payload.
alignas(16) unsigned char context_block[CONTEXT_BLOCK_SIZE];
std::mutex context_mutex;

/**
 * The 32-bit FNV-1a hash of the bytes.
 */
uint32_t fnv1a(const unsigned char *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; index++) {
    hash ^= data[index];
    hash *= 16777619u;
  }
  return hash;
}

void writeUint32(unsigned char *target, uint32_t value) {
  for (int index = 0; index < 4; index++) {
    target[index] = static_cast<unsigned char>((value >> (8 * index)) & 0xFF);
  }
}

uint32_t readUint32(const unsigned char *source) {
  uint32_t value = 0;
  for (int index = 3; index >= 0; index--) {
    value = (value << 8) | source[index];
  }
  return value;
}

bool hasDumpExtension(const std::string &name) {
  if (name.size() <= 4) {
    return false;
  }
  std::string extension = name.substr(name.size() - 4);
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension == ".dmp";
}

#ifdef _WIN32
std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &wide[0], length);
  return wide;
}

std::string narrow(const std::wstring &text) {
  if (text.empty()) {
    return std::string();
  }
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string narrowed(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &narrowed[0], length, nullptr, nullptr);
  return narrowed;
}

FILE *openFile(const std::string &path, const wchar_t *mode) { return _wfopen(widen(path).c_str(), mode); }
#else
FILE *openFile(const std::string &path, const char *mode) { return std::fopen(path.c_str(), mode); }
#endif

std::string joinPath(const std::string &directory, const std::string &name) {
  if (!directory.empty() && (directory.back() == '/' || directory.back() == '\\')) {
    return directory + name;
  }
  return directory + "/" + name;
}

#ifdef COUNTLY_USE_BREAKPAD
/**
 * The absolute form of an existing directory, so that a later change of the working directory does not move the dumps.
 */
std::string absoluteDirectory(const std::string &directory) {
#ifdef _WIN32
  const std::wstring wide = widen(directory);
  const DWORD length = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
  if (length == 0) {
    return directory;
  }
  std::wstring full(length, L'\0');
  const DWORD written = GetFullPathNameW(wide.c_str(), length, &full[0], nullptr);
  full.resize(written);
  return narrow(full);
#else
  char resolved[PATH_MAX];
  return realpath(directory.c_str(), resolved) != nullptr ? std::string(resolved) : directory;
#endif
}
#endif

} // namespace

std::vector<std::string> listDumpFiles(const std::string &directory) {
  std::vector<std::string> names;
#ifdef _WIN32
  WIN32_FIND_DATAW entry;
  HANDLE search = FindFirstFileW(widen(joinPath(directory, "*")).c_str(), &entry);
  if (search == INVALID_HANDLE_VALUE) {
    return names;
  }
  do {
    const std::string name = narrow(entry.cFileName);
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && hasDumpExtension(name)) {
      names.push_back(name);
    }
  } while (FindNextFileW(search, &entry));
  FindClose(search);
#else
  DIR *opened = opendir(directory.c_str());
  if (opened == nullptr) {
    return names;
  }
  for (struct dirent *entry = readdir(opened); entry != nullptr; entry = readdir(opened)) {
    const std::string name = entry->d_name;
    struct stat status;
    if (hasDumpExtension(name) && stat(joinPath(directory, name).c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
      names.push_back(name);
    }
  }
  closedir(opened);
#endif
  std::sort(names.begin(), names.end());
  std::vector<std::string> paths;
  for (const std::string &name : names) {
    paths.push_back(joinPath(directory, name));
  }
  return paths;
}

bool isDirectory(const std::string &path) {
#ifdef _WIN32
  const DWORD attributes = GetFileAttributesW(widen(path).c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat status;
  return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

long long fileSize(const std::string &path) {
#ifdef _WIN32
  FILE *file = openFile(path, L"rb");
#else
  FILE *file = openFile(path, "rb");
#endif
  if (file == nullptr) {
    return -1;
  }
#ifdef _WIN32
  const bool seeked = _fseeki64(file, 0, SEEK_END) == 0;
  const long long size = seeked ? _ftelli64(file) : -1;
#else
  const bool seeked = fseeko(file, 0, SEEK_END) == 0;
  const long long size = seeked ? static_cast<long long>(ftello(file)) : -1;
#endif
  std::fclose(file);
  return size;
}

bool readFile(const std::string &path, std::string &content) {
#ifdef _WIN32
  FILE *file = openFile(path, L"rb");
#else
  FILE *file = openFile(path, "rb");
#endif
  if (file == nullptr) {
    return false;
  }
  content.clear();
  char buffer[65536];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, read);
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  return !failed;
}

bool removeFile(const std::string &path) {
#ifdef _WIN32
  return _wremove(widen(path).c_str()) == 0;
#else
  return std::remove(path.c_str()) == 0;
#endif
}

std::string base64Encode(const std::string &bytes) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);
  size_t index = 0;
  for (; index + 2 < bytes.size(); index += 3) {
    const uint32_t triple = (static_cast<unsigned char>(bytes[index]) << 16) | (static_cast<unsigned char>(bytes[index + 1]) << 8) | static_cast<unsigned char>(bytes[index + 2]);
    encoded.push_back(alphabet[(triple >> 18) & 0x3F]);
    encoded.push_back(alphabet[(triple >> 12) & 0x3F]);
    encoded.push_back(alphabet[(triple >> 6) & 0x3F]);
    encoded.push_back(alphabet[triple & 0x3F]);
  }
  const size_t rest = bytes.size() - index;
  if (rest > 0) {
    uint32_t triple = static_cast<unsigned char>(bytes[index]) << 16;
    if (rest == 2) {
      triple |= static_cast<unsigned char>(bytes[index + 1]) << 8;
    }
    encoded.push_back(alphabet[(triple >> 18) & 0x3F]);
    encoded.push_back(alphabet[(triple >> 12) & 0x3F]);
    encoded.push_back(rest == 2 ? alphabet[(triple >> 6) & 0x3F] : '=');
    encoded.push_back('=');
  }
  return encoded;
}

size_t maxContextPayload() { return CONTEXT_BLOCK_SIZE - CONTEXT_HEADER_SIZE; }

std::string encodeContextBlock(const std::string &payload) {
  if (payload.size() > maxContextPayload()) {
    return std::string();
  }
  std::string block(CONTEXT_HEADER_SIZE + payload.size(), '\0');
  unsigned char *bytes = reinterpret_cast<unsigned char *>(&block[0]);
  std::memcpy(bytes, CONTEXT_MAGIC, sizeof(CONTEXT_MAGIC));
  writeUint32(bytes + 16, static_cast<uint32_t>(payload.size()));
  writeUint32(bytes + 20, fnv1a(reinterpret_cast<const unsigned char *>(payload.data()), payload.size()));
  std::memcpy(bytes + CONTEXT_HEADER_SIZE, payload.data(), payload.size());
  return block;
}

bool findContextPayload(const std::string &dump, std::string &payload) {
  const std::string magic(CONTEXT_MAGIC, sizeof(CONTEXT_MAGIC));
  for (std::string::size_type at = dump.find(magic); at != std::string::npos; at = dump.find(magic, at + 1)) {
    if (dump.size() - at < CONTEXT_HEADER_SIZE) {
      break;
    }
    const unsigned char *header = reinterpret_cast<const unsigned char *>(dump.data() + at);
    const uint32_t length = readUint32(header + 16);
    if (length == 0 || length > maxContextPayload() || dump.size() - at - CONTEXT_HEADER_SIZE < length) {
      continue;
    }
    const unsigned char *candidate = header + CONTEXT_HEADER_SIZE;
    if (fnv1a(candidate, length) == readUint32(header + 20)) {
      payload.assign(reinterpret_cast<const char *>(candidate), length);
      return true;
    }
  }
  return false;
}

bool setContextPayload(const std::string &payload) {
  std::lock_guard<std::mutex> lock(context_mutex);
  // the length goes last, and the fences keep the compiler from moving it before the payload: a dump written in the
  // middle of the change holds either the old block or no valid block
  writeUint32(context_block + 16, 0);
  std::atomic_signal_fence(std::memory_order_seq_cst);
  if (payload.size() > maxContextPayload()) {
    return false;
  }
  std::memcpy(context_block, CONTEXT_MAGIC, sizeof(CONTEXT_MAGIC));
  std::memcpy(context_block + CONTEXT_HEADER_SIZE, payload.data(), payload.size());
  writeUint32(context_block + 20, fnv1a(reinterpret_cast<const unsigned char *>(payload.data()), payload.size()));
  std::atomic_signal_fence(std::memory_order_seq_cst);
  writeUint32(context_block + 16, static_cast<uint32_t>(payload.size()));
  std::atomic_signal_fence(std::memory_order_seq_cst);
  return true;
}

bool isHandlerAvailable() {
#ifdef COUNTLY_USE_BREAKPAD
  return true;
#else
  return false;
#endif
}

bool installHandler(const std::string &directory, std::string &error) {
#ifdef COUNTLY_USE_BREAKPAD
  if (!isDirectory(directory)) {
    error = "the crash dump directory [" + directory + "] does not exist";
    return false;
  }
  if (!breakpad::install(absoluteDirectory(directory), context_block, sizeof(context_block))) {
    error = "a crash handler is already installed";
    return false;
  }
  return true;
#else
  (void)directory;
  error = "this build has no crash handler, build the SDK with COUNTLY_USE_BREAKPAD";
  return false;
#endif
}

void uninstallHandler() {
#ifdef COUNTLY_USE_BREAKPAD
  breakpad::uninstall();
#endif
}

bool isHandlerInstalled() {
#ifdef COUNTLY_USE_BREAKPAD
  return breakpad::isInstalled();
#else
  return false;
#endif
}

} // namespace native_crash
} // namespace cly
