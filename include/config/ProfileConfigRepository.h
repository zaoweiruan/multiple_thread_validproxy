#ifndef PROFILE_CONFIG_REPOSITORY_H
#define PROFILE_CONFIG_REPOSITORY_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include "Profileitem.h"
#include "ProfileExItem.h"

namespace config {

class ProfileConfigRepository {
public:
    explicit ProfileConfigRepository(sqlite3* db);
    std::vector<db::models::Profileitem> loadProfiles(const std::string& sqlQuery = "");
    std::vector<db::models::ProfileExItem> loadProfileExItems();
    bool updateProfileExItem(const db::models::ProfileExItem& exitem);
private:
    sqlite3* db_;
};

} // namespace config

#endif // PROFILE_CONFIG_REPOSITORY_H
