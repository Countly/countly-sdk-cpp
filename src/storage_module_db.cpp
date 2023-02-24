
#include "countly/storage_module_db.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#ifdef COUNTLY_USE_SQLITE
#include "sqlite3.h"
#endif
#include <sstream>

const char REQUESTS_TABLE_NAME[] = "Requests";
const char REQUESTS_TABLE_REQUEST_ID[] = "RequestID";
const char REQUESTS_TABLE_REQUEST_DATA[] = "RequestData";

namespace cly {
StorageModuleDB::StorageModuleDB(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleDB::~StorageModuleDB() {}

void StorageModuleDB::init() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] initialized.");
  if (_configuration->databasePath == "" || _configuration->databasePath == " ") {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] init: Database path can not be empty or blank!");
    return;
  }

  createSchema(REQUESTS_TABLE_NAME, REQUESTS_TABLE_REQUEST_ID, REQUESTS_TABLE_REQUEST_DATA);
  _is_initialized = true;
}

void StorageModuleDB::createSchema(const char tableName[], const char keyColumnName[], const char dataColumnName[]) {
  _logger->log(LogLevel::INFO, "[StorageModuleDB][createSchema]");

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "CREATE TABLE IF NOT EXISTS " << tableName << " (" << keyColumnName << " INTEGER PRIMARY KEY, " << dataColumnName << " TEXT)";

    std::string statement = sql_statement_stream.str();
    return_value = sqlite3_exec(database, statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      _logger->log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    }
  } else {
    std::string error(error_message);
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB][createSchema] Failed to open sqlite database error = " + error);
    sqlite3_free(error_message);
  }
  sqlite3_close(database);
#endif
}

void StorageModuleDB::RQRemoveFront() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront");

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << " WHERE " << REQUESTS_TABLE_REQUEST_ID << " = ( SELECT MIN(" << REQUESTS_TABLE_REQUEST_ID << ") FROM " << REQUESTS_TABLE_NAME << " );";
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront SQL = " + sql_statement_stream.str());

    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
#endif
}

void StorageModuleDB::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront(request): Module is not initialized");
    return;
  }

  if (request == nullptr) {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleDB] RQRemoveFront request = null");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront RequestID = " + request->getId());

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << " WHERE " << REQUESTS_TABLE_REQUEST_ID << " = " << request->getId() << ';';
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront SQL = " + sql_statement_stream.str());

    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
#endif
}

long long StorageModuleDB::RQCount() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQCount: Module is not initialized");
    return -1;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount");
  long long requestCount = 0;

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT COUNT(*) FROM " << REQUESTS_TABLE_NAME << ";";
    return_value = sqlite3_get_table(database, sql_statement_stream.str().c_str(), &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      requestCount = atoll(table[1]);
    } else {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQCount error = " + error);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  }
  sqlite3_close(database);
#endif

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount requests count = " + std::to_string(requestCount));
  return requestCount;
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleDB::RQPeekAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekAll: Module is not initialized");
    return {};
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekAll");

  std::vector<std::shared_ptr<DataEntry>> v;

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT * FROM " << REQUESTS_TABLE_NAME << " ORDER BY " << REQUESTS_TABLE_REQUEST_ID << " ASC;";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    bool no_request = (row_count == 0);
    if (return_value == SQLITE_OK && !no_request) {

      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        std::string rqstId = table[event_index * column_count];
        std::string rqst = table[(event_index * column_count) + 1];
        v.push_back(std::make_shared<DataEntry>(std::stoll(rqstId), rqst));
      }

    } else if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekAll error =" + error);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  }
  sqlite3_close(database);
#endif
  return v;
}

void StorageModuleDB::RQInsertAtEnd(const std::string &request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQInsertAtEnd: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQInsertAtEnd request = " + request);

  if (request == "") {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQInsertAtEnd request is empty");
    return;
  }

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "INSERT INTO " << REQUESTS_TABLE_NAME << " (" << REQUESTS_TABLE_REQUEST_DATA << ") VALUES('" << request << "');";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQInsertAtEnd error =" + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
#endif
}

void StorageModuleDB::RQClearAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQClearAll: Module is not initialized");
    return;
  }
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQClearAll");

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << ";";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
#endif
}

const std::shared_ptr<DataEntry> StorageModuleDB::RQPeekFront() {
  std::shared_ptr<DataEntry> front = std::make_shared<DataEntry>(-1, "");
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekFront: Module is not initialized");
    return front;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFronts");

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT " << REQUESTS_TABLE_REQUEST_ID << ", " << REQUESTS_TABLE_REQUEST_DATA << " FROM " << REQUESTS_TABLE_NAME << " ORDER BY " << REQUESTS_TABLE_REQUEST_ID << " ASC LIMIT 1;";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    bool no_request = (row_count == 0);
    if (return_value == SQLITE_OK && !no_request) {

      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        std::string rqstId = table[event_index * column_count];
        std::string rqst = table[(event_index * column_count) + 1];
        DataEntry *frontEntry = new DataEntry(std::stoll(rqstId), rqst);
        _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFronts id =" + rqstId);
        front.reset(frontEntry);
      }
    } else if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekFronts error =" + error);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  }
  sqlite3_close(database);
#endif

  return front;
}

}; // namespace cly