#ifndef COUNTLY_PATH_UTILS_HPP_
#define COUNTLY_PATH_UTILS_HPP_

#include <string>
#include <vector>

namespace cly {
namespace utils {

/**
 * Lexically normalizes a database path so two spellings of the same path
 * compare equal. Purely textual: it never touches the filesystem, so it works
 * for a database file that does not exist yet.
 *
 * This is a guardrail, not a boundary. It does not resolve symlinks,
 * hardlinks, SQLite URI filenames (`file:a.db?mode=rwc`) or Windows 8.3 short
 * names.
 */
inline std::string normalizeDatabasePath(const std::string &path) {
  // 1. Trim surrounding whitespace.
  static const char *const WHITESPACE = " \t\n\r\f\v";
  const std::string::size_type first = path.find_first_not_of(WHITESPACE);
  if (first == std::string::npos) {
    return "";
  }
  const std::string::size_type last = path.find_last_not_of(WHITESPACE);
  std::string working = path.substr(first, last - first + 1);

  // 2. Unify separators.
  for (std::string::size_type index = 0; index < working.size(); ++index) {
    if (working[index] == '\\') {
      working[index] = '/';
    }
  }

  // 3. Split off a root prefix so that "//host/share" and "/var" survive the
  //    empty-segment collapse below.
  std::string prefix;
  if (working.compare(0, 2, "//") == 0) {
    prefix = "//";
    working = working.substr(2);
  } else if (!working.empty() && working[0] == '/') {
    prefix = "/";
    working = working.substr(1);
  }

  // 4 + 5. Walk the segments: drop empties (collapsed separators, trailing
  //        slash) and ".", resolve ".." lexically.
  std::vector<std::string> segments;
  std::string::size_type start = 0;
  while (true) {
    const std::string::size_type slash = working.find('/', start);
    const std::string segment = (slash == std::string::npos) ? working.substr(start) : working.substr(start, slash - start);

    if (segment.empty() || segment == ".") {
      // nothing to add
    } else if (segment == "..") {
      if (!segments.empty() && segments.back() != "..") {
        segments.pop_back();
      } else if (prefix.empty()) {
        segments.push_back(segment); // a leading ".." on a relative path is meaningful
      }
      // ".." at the root of an absolute path has nowhere to go; drop it
    } else {
      segments.push_back(segment);
    }

    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }

  // 6. Rejoin.
  std::string result = prefix;
  for (std::vector<std::string>::size_type index = 0; index < segments.size(); ++index) {
    if (index > 0) {
      result += "/";
    }
    result += segments[index];
  }

  // 7. Case-insensitive filesystem.
#ifdef _WIN32
  for (std::string::size_type index = 0; index < result.size(); ++index) {
    if (result[index] >= 'A' && result[index] <= 'Z') {
      result[index] = static_cast<char>(result[index] - 'A' + 'a');
    }
  }
#endif

  return result;
}

} // namespace utils
} // namespace cly
#endif
