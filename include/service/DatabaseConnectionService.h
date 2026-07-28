#ifndef SERVICE_DATABASE_CONNECTION_SERVICE_H
#define SERVICE_DATABASE_CONNECTION_SERVICE_H

#include <string>
#include <sqlite3.h>

namespace service {

class DatabaseConnectionService {
public:
    DatabaseConnectionService();

    sqlite3* open(const std::string& path);
    bool close(sqlite3* db);
    bool isValid(sqlite3* db) const;

    static void applyPragmas(sqlite3* db);
};

} // namespace service

#endif // SERVICE_DATABASE_CONNECTION_SERVICE_H