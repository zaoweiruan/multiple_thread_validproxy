#ifndef DB_HELPER_H
#define DB_HELPER_H

#include <string>
#include <memory>
#include <sqlite3.h>
#include <iostream>
#include "Logger.h"

namespace db {

class Database {
private:
  sqlite3* db_;
  std::string db_path_;

  // 禁止拷贝
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

public:
  explicit Database(const std::string& db_path) : db_path_(db_path), db_(nullptr) {}

  ~Database() { close(); }

  bool open() {
    if (db_) {
      close();
    }
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
      std::string errMsg = db_ ? sqlite3_errmsg(db_) : "database handle is null or invalid";
      Logger::write("无法打开数据库: " + errMsg, LogLevel::ERR);
      return false;
    }
    return true;
  }

  bool execute(const std::string& sql) {
    if (!db_) return false;

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
      if (err_msg) {
        Logger::write("SQL错误: " + std::string(err_msg), LogLevel::ERR);
        sqlite3_free(err_msg);
      }
      return false;
    }
    return true;
  }

  sqlite3* get() const { return db_; }

  void close() {
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  bool isOpen() const { return db_ != nullptr; }

  int64_t lastInsertRowId() const {
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
  }
};

} // namespace db

#endif // DB_HELPER_H