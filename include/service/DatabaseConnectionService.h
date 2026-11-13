#ifndef SERVICE_DATABASE_CONNECTION_SERVICE_H
#define SERVICE_DATABASE_CONNECTION_SERVICE_H

#include <string>
#include <sqlite3.h>

namespace service {

// How to open the database.
//   ReadWriteCreate   - legacy behaviour: create a 0-byte DB when the file is missing.
//   ReadWriteExisting - never create; the caller has already verified the file exists.
// FULLMUTEX is applied in BOTH modes (the app shares one connection across threads).
enum class OpenMode { ReadWriteCreate, ReadWriteExisting };

class DatabaseConnectionService {
public:
    DatabaseConnectionService();

    sqlite3* open(const std::string& path, OpenMode mode = OpenMode::ReadWriteCreate);
    bool close(sqlite3* db);
    bool isValid(sqlite3* db) const;

    static void applyPragmas(sqlite3* db);
};

} // namespace service

#endif // SERVICE_DATABASE_CONNECTION_SERVICE_H