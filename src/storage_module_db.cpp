
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

#ifdef COUNTLY_USE_SQLITE
  // Check if the database path is empty or blank
  if (_configuration->databasePath == "" || _configuration->databasePath == " ") {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] init: Database path can not be empty or blank!");
    return;
  }
#endif

  // Create schema for the requests table
  _is_initialized = createSchema(REQUESTS_TABLE_NAME, REQUESTS_TABLE_REQUEST_ID, REQUESTS_TABLE_REQUEST_DATA);

  if (_is_initialized) {
    vacuumDatabase();
  }
}

void StorageModuleDB::vacuumDatabase() {
  _logger->log(LogLevel::INFO, "[StorageModuleDB][Vacuum] Will try to vacuum the database");

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    return_value = sqlite3_exec(database, "VACUUM", nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      _logger->log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    } else {
      _logger->log(LogLevel::INFO, "[StorageModuleDB][Vacuum] Database vacuumed successfully");
    }
  } else {
    std::string error(error_message);
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB][Vacuum] Failed to open sqlite database error = " + error);
    sqlite3_free(error_message);
  }
  sqlite3_close(database);
#endif
}

bool StorageModuleDB::createSchema(const char tableName[], const char keyColumnName[], const char dataColumnName[]) {
  _logger->log(LogLevel::INFO, "[StorageModuleDB][createSchema]");

  bool result = false;
#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;

  // Open the SQLite database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    // Create the table if it does not exist
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "CREATE TABLE IF NOT EXISTS " << tableName << " (" << keyColumnName << " INTEGER PRIMARY KEY, " << dataColumnName << " TEXT)";

    std::string statement = sql_statement_stream.str();

    // Execute the SQL statement
    return_value = sqlite3_exec(database, statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      _logger->log(LogLevel::ERROR, error_message);
      sqlite3_free(error_message);
    } else {
      result = true;
    }
  } else {
    std::string error(error_message);
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB][createSchema] Failed to open sqlite database error = " + error);
    sqlite3_free(error_message);
  }

  // Close the SQLite database
  sqlite3_close(database);
#endif

  return result;
}

// Remove the first item from the SQLite database's requests table
void StorageModuleDB::RQRemoveFront() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront: Module is not initialized");
    return;
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront");

#ifdef COUNTLY_USE_SQLITE
  // Declare SQLite database, return value and error message variables
  sqlite3 *database;
  int return_value;
  char *error_message;
  // Open the SQLite database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) { // Check if the SQL statement execution is successful
    // Remove the first entry in the requests table
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << " WHERE " << REQUESTS_TABLE_REQUEST_ID << " = ( SELECT MIN(" << REQUESTS_TABLE_REQUEST_ID << ") FROM " << REQUESTS_TABLE_NAME << " );";
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront SQL = " + sql_statement_stream.str());

    std::string sql_statement = sql_statement_stream.str();

    // Execute the SQL statement
    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  // Close the database
  sqlite3_close(database);
#endif
}

void StorageModuleDB::RQRemoveFront(std::shared_ptr<DataEntry> request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront(request): Module is not initialized");
    return;
  }

  if (request == nullptr) {
    // Check if request is null
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleDB] RQRemoveFront request = null");
    return;
  }

  // Log the request ID being removed
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront RequestID = " + request->getId());

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value;
  char *error_message;
  // Open the SQLite database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    // Build SQL statement to remove request from database
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << " WHERE " << REQUESTS_TABLE_REQUEST_ID << " = " << request->getId() << ';';
    _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQRemoveFront SQL = " + sql_statement_stream.str());

    std::string sql_statement = sql_statement_stream.str();

    // Execute the SQL statement
    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  // Close the database connection
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
  // Define variables for SQLite database
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  // Open the SQLite database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    // Define the SQL statement for counting the number of rows in the requests table
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT COUNT(*) FROM " << REQUESTS_TABLE_NAME << ";";
    // Execute the SQL statement
    return_value = sqlite3_get_table(database, sql_statement_stream.str().c_str(), &table, &row_count, &column_count, &error_message);
    if (return_value == SQLITE_OK) {
      // Parse the result and assign the number of rows to requestCount
      requestCount = atoll(table[1]);
    } else {
      // Log any errors encountered during the execution of the SQL statement
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQCount error = " + error);
      sqlite3_free(error_message);
    }
    // Free the memory allocated for the result table
    sqlite3_free_table(table);
  }
  // Close the SQLite database
  sqlite3_close(database);
#endif

  // Log the number of requests in the requests table
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQCount requests count = " + std::to_string(requestCount));
  // Return the number of requests
  return requestCount;
}

std::vector<std::shared_ptr<DataEntry>> StorageModuleDB::RQPeekAll() {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekAll: Module is not initialized");
    return {}; // Return an empty vector if the module is not initialized
  }

  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekAll");

  std::vector<std::shared_ptr<DataEntry>> v; // Initialize a vector to store the data entries

#ifdef COUNTLY_USE_SQLITE
  sqlite3 *database;
  int return_value, row_count, column_count;
  char **table;
  char *error_message;

  // Open the database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT * FROM " << REQUESTS_TABLE_NAME << " ORDER BY " << REQUESTS_TABLE_REQUEST_ID << " ASC;";
    std::string sql_statement = sql_statement_stream.str();

    // Execute the query to get all the data entries
    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    // Check if there are any data entries in the table
    bool no_request = (row_count == 0);
    // If the query was successful and there are data entries in the table
    if (return_value == SQLITE_OK && !no_request) {

      // Loop through all the data entries and add them to the vector
      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        std::string requestId = table[event_index * column_count];
        std::string request = table[(event_index * column_count) + 1];
        v.push_back(std::make_shared<DataEntry>(std::stoll(requestId), request));
      }

    } else if (return_value != SQLITE_OK) { // If there was an error executing the query
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekAll error =" + error);
      sqlite3_free(error_message);
    }
    // Free the result table
    sqlite3_free_table(table);
  }
  // Close the database
  sqlite3_close(database);
#endif
  return v; // Return the vector containing all the data entries
}

void StorageModuleDB::RQInsertAtEnd(const std::string &request) {
  if (!_is_initialized) {
    _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQInsertAtEnd: Module is not initialized");
    return; // Checks if the module is initialized, returns if not
  }

  // Logs the request being inserted
  _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQInsertAtEnd request = " + request);

  if (request == "") {
    _logger->log(LogLevel::WARNING, "[Countly][StorageModuleMemory] RQInsertAtEnd request is empty");
    return; // Checks if the request is empty, logs a warning and returns if it is
  }

#ifdef COUNTLY_USE_SQLITE
  // Initializes variables for working with SQLite
  sqlite3 *database;
  int return_value;
  char *error_message;

  // Opens the database connection
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    // Prepares the SQL statement for inserting the request into the database
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
  // Closes the database connection
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
  // Open database connection
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "DELETE FROM " << REQUESTS_TABLE_NAME << ";";
    std::string sql_statement = sql_statement_stream.str();

    // Execute SQL statement to delete all requests
    return_value = sqlite3_exec(database, sql_statement.c_str(), nullptr, nullptr, &error_message);
    if (return_value != SQLITE_OK) { // Check if SQL statement executed successfully
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQRemoveFront error = " + error);
      sqlite3_free(error_message);
    }
  }
  // Close database connection
  sqlite3_close(database);
#endif
}

const std::shared_ptr<DataEntry> StorageModuleDB::RQPeekFront() {
  std::shared_ptr<DataEntry> front = std::make_shared<DataEntry>(-1, ""); // Initialize a shared pointer to a default-constructed DataEntry object
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

  // Open the SQLite database
  return_value = sqlite3_open(_configuration->databasePath.c_str(), &database);
  if (return_value == SQLITE_OK) {
    // Construct an SQL statement to retrieve the first row of the requests table
    std::ostringstream sql_statement_stream;
    sql_statement_stream << "SELECT " << REQUESTS_TABLE_REQUEST_ID << ", " << REQUESTS_TABLE_REQUEST_DATA << " FROM " << REQUESTS_TABLE_NAME << " ORDER BY " << REQUESTS_TABLE_REQUEST_ID << " ASC LIMIT 1;";
    std::string sql_statement = sql_statement_stream.str();

    // Execute the SQL statement
    return_value = sqlite3_get_table(database, sql_statement.c_str(), &table, &row_count, &column_count, &error_message);
    bool no_request = (row_count == 0);             // Check if there are any rows in the requests table
    if (return_value == SQLITE_OK && !no_request) { // If the SQL statement is executed successfully and there is at least one row in the requests table
      // For each column in the first row of the requests table
      for (int event_index = 1; event_index < row_count + 1; event_index++) {
        // Retrieve the request ID and request data from the table
        std::string requestId = table[event_index * column_count];
        std::string request = table[(event_index * column_count) + 1];
        // Create a new DataEntry object and reset the shared pointer to point to it
        DataEntry *frontEntry = new DataEntry(std::stoll(requestId), request);
        _logger->log(LogLevel::DEBUG, "[Countly][StorageModuleDB] RQPeekFronts id =" + requestId);
        front.reset(frontEntry);
      }
    } else if (return_value != SQLITE_OK) {
      std::string error(error_message);
      _logger->log(LogLevel::ERROR, "[Countly][StorageModuleDB] RQPeekFronts error =" + error);
      sqlite3_free(error_message); // free the error message pointer
    }
    sqlite3_free_table(table); // Free the table pointer
  }
  sqlite3_close(database); // Close the SQLite database
#endif

  return front; // Return the shared pointer to the DataEntry object
}

}; // namespace cly