#ifndef COUNTLY_SQLITE_UTILS_HPP_
#define COUNTLY_SQLITE_UTILS_HPP_

#ifdef COUNTLY_USE_SQLITE

#include "sqlite3.h"
#include <string>

/**
 * How long a connection waits for whoever holds the database lock before giving
 * up with SQLITE_BUSY.
 *
 * Every database operation in the SDK opens its own connection, uses it and
 * closes it, so the lock is only ever held for the length of one statement --
 * no HTTP call or other blocking work happens with the lock held. Three seconds
 * is therefore far more than contention needs, and short enough that a genuinely
 * stuck database still surfaces as an error instead of hanging.
 */
#define COUNTLY_SQLITE_BUSY_TIMEOUT_MS 3000

namespace cly {
namespace utils {

/**
 * Opens a SQLite database with a busy timeout set.
 *
 * Always use this instead of sqlite3_open. Without a busy timeout the default is
 * zero: a connection that finds the database locked fails immediately with
 * SQLITE_BUSY. That happens routinely, because the SDK reads the queues from one
 * thread while writing them from another -- the event queue size check
 * deliberately drops the instance mutex before querying -- and a failed write is
 * a silently dropped event or request.
 *
 * @param path: database file path
 * @param database: receives the connection, and must be closed by the caller
 *                  even when this fails, exactly as with sqlite3_open
 * @return the sqlite3_open result code
 */
inline int openDatabase(const std::string &path, sqlite3 **database) {
  const int return_value = sqlite3_open(path.c_str(), database);
  if (*database != nullptr) {
    // Worth setting even when the open failed: the handle still has to be
    // closed, and a later call on it should not fail for lack of a timeout.
    sqlite3_busy_timeout(*database, COUNTLY_SQLITE_BUSY_TIMEOUT_MS);
  }
  return return_value;
}

} // namespace utils
} // namespace cly

#endif // COUNTLY_USE_SQLITE
#endif
