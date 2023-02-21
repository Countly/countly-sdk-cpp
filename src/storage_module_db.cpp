
#include "countly/storage_module_db.hpp"
#include "countly/countly_configuration.hpp"
#include "countly/logger_module.hpp"
#include "sqlite3.h"
#include <sstream>

namespace cly {
StorageModuleDB::StorageModuleDB(std::shared_ptr<CountlyConfiguration> config, std::shared_ptr<LoggerModule> logger) : StorageModuleBase(config, logger) {}

StorageModuleDB::~StorageModuleDB() {}

void StorageModuleDB::init() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] initialized.");
  createSchema();
}

void StorageModuleDB::createSchema() {
  _logger->log(LogLevel::INFO, "[StorageModuleDB][createSchema]");

  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS Requests (RequestID INTEGER PRIMARY KEY, RequestData TEXT)", nullptr, nullptr, &error_message);
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
}

void StorageModuleDB::RQRemoveFront() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront");

  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM Requests WHERE RequestID = ( SELECT MIN(RequestID) FROM Requests );";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
}

void StorageModuleDB::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (request == nullptr) {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleDB] RQRemoveFront request = null");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront RequestID = " + request->getId());

  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM Requests WHERE RequestID = " << request->getId() << ';';
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
}

int StorageModuleDB::RQCount() {
  int size = 0;
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount size = " + size);

  return 0;
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleDB::RQPeekAll() {

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekAll");

  std::vector<std::shared_ptr<DataEntry>> v;
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;
  std::string event_ids;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT * FROM Requests ORDER BY RequestID ASC;";
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

  return v;
}

void StorageModuleDB::RQInsertAtEnd(const std::string &request) {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQInsertAtEnd request = " + request);

  sqlite3 *database;
  int return_value;
  char *error_message;

  return_value = sqlite3_open(database_path.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "INSERT INTO Requests (RequestData) VALUES('" << request << "');";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQInsertAtEnd error =" + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
}

void StorageModuleDB::RQClearAll() {
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQClearAll");

  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM Requests;";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  sqlite3_close(database);
}

const std::shared_ptr<DataEntry> StorageModuleDB::RQPeekFront() {
  std::shared_ptr<DataEntry> front = nullptr;
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFronts");

  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;
  std::string event_ids;

  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT * FROM Requests ORDER BY RequestID ASC LIMIT 1;";
    std::string sql_statement = sql_statement_stream.str();

    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    bool no_request = (row_count == 0);
    if (return_value == SQLITE_OK && !no_request) {

      std::string rqstId = table[1 * column_count];
      std::string rqst = table[(1 * column_count) + 1];
      DataEntry *frontEntry = new DataEntry(std::stoll(rqstId), rqst);
      front.reset(frontEntry);
    } else if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekFronts error =" + error);
      sqlite3_free(error_message);
    }
    sqlite3_free_table(table);
  }
  sqlite3_close(database);

  return front;
}

}; // namespace cly