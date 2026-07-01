#ifndef COUNTLY_INTERNAL_LIMITS_HPP_
#define COUNTLY_INTERNAL_LIMITS_HPP_

#include "countly/configuration_provider.hpp" // for cly::SDKLimits

#include <map>
#include <sstream>
#include <string>

namespace cly {
namespace limits {

// Truncate a string to at most maxLen bytes without splitting a multibyte
// UTF-8 sequence at the cut point.
inline std::string truncateString(const std::string &s, unsigned int maxLen) {
  if (s.size() <= static_cast<size_t>(maxLen)) {
    return s;
  }
  size_t cut = maxLen;
  // UTF-8 continuation bytes are 0b10xxxxxx. If the cut lands on one, we are
  // inside a character; back off until it sits on a code-point boundary.
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return s.substr(0, cut);
}

// Truncate keys/values of a developer-supplied segmentation map and cap the
// entry count. std::map iterates in sorted key order, so the retained subset
// is deterministic.
inline std::map<std::string, std::string> applySegmentationLimits(const std::map<std::string, std::string> &in, const SDKLimits &lim) {
  std::map<std::string, std::string> out;
  unsigned int kept = 0;
  for (const auto &kv : in) {
    if (kept >= lim.maxSegmentationValues) {
      break;
    }
    out[truncateString(kv.first, lim.maxKeyLength)] = truncateString(kv.second, lim.maxValueSize);
    ++kept;
  }
  return out;
}

// Cap a stack-trace string to maxLines lines and each line to maxLineLength
// bytes. Lines are split on '\n' and rejoined with '\n'.
inline std::string truncateStackTrace(const std::string &trace, unsigned int maxLines, unsigned int maxLineLength) {
  std::istringstream stream(trace);
  std::string line;
  std::string result;
  unsigned int count = 0;
  bool first = true;
  while (count < maxLines && std::getline(stream, line)) {
    if (!first) {
      result += "\n";
    }
    result += truncateString(line, maxLineLength);
    first = false;
    ++count;
  }
  return result;
}

} // namespace limits
} // namespace cly

#endif
