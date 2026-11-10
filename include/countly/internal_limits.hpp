#ifndef COUNTLY_INTERNAL_LIMITS_HPP_
#define COUNTLY_INTERNAL_LIMITS_HPP_

#include "countly/configuration_provider.hpp" // for cly::SDKLimits
#include "countly/logger_module.hpp"
#include "nlohmann/json.hpp"

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

/**
 * The most characters a stack trace keeps, whatever its line limits allow.
 */
const size_t MAX_STACK_TRACE_LENGTH = 20000;

/**
 * Cuts a key to the key length limit. A cut key is logged as a warning and counted as log code w201.
 *
 * @param key: the key
 * @param maxLength: the key length limit
 * @param logger: logs the cut, may be null
 * @param where: the module and method, for example "[ViewsModule] openView"
 * @return the key, cut when it was too long
 */
inline std::string limitKey(const std::string &key, unsigned int maxLength, LoggerModule *logger, const std::string &where) {
  const std::string limited = truncateString(key, maxLength);
  if (logger != nullptr && limited.size() != key.size()) {
    logger->log(LogLevel::WARNING, "[Countly] " + where + ", The key [" + limited + "...] is longer than the key length limit of [" + std::to_string(maxLength) + "] and is cut.");
    logger->recordLogCode("w201");
  }
  return limited;
}

/**
 * Cuts a value to its size limit. A cut value is logged as a warning and counted as log code w202.
 *
 * @param value: the value
 * @param maxLength: the size limit
 * @param logger: logs the cut, may be null
 * @param where: the module and method
 * @param name: what the value belongs to, for example its segmentation key
 * @return the value, cut when it was too long
 */
inline std::string limitValue(const std::string &value, unsigned int maxLength, LoggerModule *logger, const std::string &where, const std::string &name) {
  const std::string limited = truncateString(value, maxLength);
  if (logger != nullptr && limited.size() != value.size()) {
    logger->log(LogLevel::WARNING, "[Countly] " + where + ", The value of [" + name + "] is longer than the size limit of [" + std::to_string(maxLength) + "] and is cut.");
    logger->recordLogCode("w202");
  }
  return limited;
}

/**
 * Logs the segmentation entries dropped because of the entry count limit, as a warning counted as log code w203.
 */
inline void logDroppedEntries(const std::string &dropped, unsigned int maxEntries, LoggerModule *logger, const std::string &where) {
  if (logger != nullptr && !dropped.empty()) {
    logger->log(LogLevel::WARNING, "[Countly] " + where + ", The segmentation has more than [" + std::to_string(maxEntries) + "] entries, so these are dropped: [" + dropped + "]");
    logger->recordLogCode("w203");
  }
}

/**
 * Applies the key length, value size and entry count limits to a segmentation map, logging what it cuts or drops.
 */
inline std::map<std::string, std::string> limitSegmentation(const std::map<std::string, std::string> &in, const SDKLimits &lim, LoggerModule *logger, const std::string &where) {
  std::map<std::string, std::string> out;
  std::string dropped;
  unsigned int kept = 0;
  for (const auto &kv : in) {
    if (kept >= lim.maxSegmentationValues) {
      dropped += (dropped.empty() ? "" : ", ") + kv.first;
      continue;
    }
    out[limitKey(kv.first, lim.maxKeyLength, logger, where)] = limitValue(kv.second, lim.maxValueSize, logger, where, kv.first);
    ++kept;
  }
  logDroppedEntries(dropped, lim.maxSegmentationValues, logger, where);
  return out;
}

/**
 * Applies the key length, value size and entry count limits to a segmentation object, logging what it cuts or
 * drops. Values that are not strings are kept as they are.
 */
inline void limitSegmentation(nlohmann::json &segmentation, const SDKLimits &lim, LoggerModule *logger, const std::string &where) {
  if (!segmentation.is_object()) {
    return;
  }
  nlohmann::json limited = nlohmann::json::object();
  std::string dropped;
  unsigned int kept = 0;
  for (nlohmann::json::const_iterator it = segmentation.begin(); it != segmentation.end(); ++it) {
    if (kept >= lim.maxSegmentationValues) {
      dropped += (dropped.empty() ? "" : ", ") + it.key();
      continue;
    }
    const std::string key = limitKey(it.key(), lim.maxKeyLength, logger, where);
    limited[key] = it.value().is_string() ? nlohmann::json(limitValue(it.value().get<std::string>(), lim.maxValueSize, logger, where, it.key())) : it.value();
    ++kept;
  }
  logDroppedEntries(dropped, lim.maxSegmentationValues, logger, where);
  segmentation = limited;
}

/**
 * Cuts an error text, such as a crash title, to the stack trace line length limit. A cut text is logged as a warning
 * and counted as log code w205.
 */
inline std::string limitErrorText(const std::string &text, unsigned int maxLength, LoggerModule *logger, const std::string &where) {
  const std::string limited = truncateString(text, maxLength);
  if (logger != nullptr && limited.size() != text.size()) {
    logger->log(LogLevel::WARNING, "[Countly] " + where + ", The error text is longer than the limit of [" + std::to_string(maxLength) + "] characters and is cut.");
    logger->recordLogCode("w205");
  }
  return limited;
}

/**
 * Applies the line count and line length limits to a stack trace and caps it at MAX_STACK_TRACE_LENGTH characters.
 * A cut stack trace is logged as a warning and counted as log code w205.
 */
inline std::string limitStackTrace(const std::string &trace, unsigned int maxLines, unsigned int maxLineLength, LoggerModule *logger, const std::string &where) {
  std::string limited = truncateStackTrace(trace, maxLines, maxLineLength);
  bool cut = false;
  std::istringstream stream(trace);
  std::string line;
  unsigned int count = 0;
  while (std::getline(stream, line)) {
    if (++count > maxLines || line.size() > maxLineLength) {
      cut = true;
      break;
    }
  }
  if (limited.size() > MAX_STACK_TRACE_LENGTH) {
    limited = truncateString(limited, static_cast<unsigned int>(MAX_STACK_TRACE_LENGTH));
    cut = true;
  }
  if (logger != nullptr && cut) {
    logger->log(LogLevel::WARNING, "[Countly] " + where + ", The stack trace is longer than its limits of [" + std::to_string(maxLines) + "] lines of [" + std::to_string(maxLineLength) + "] characters and is cut.");
    logger->recordLogCode("w205");
  }
  return limited;
}

} // namespace limits
} // namespace cly

#endif
