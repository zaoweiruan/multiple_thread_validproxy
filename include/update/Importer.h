#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include "ConfigReader.h"

namespace update {

class Importer {
public:
    Importer(sqlite3* db, const config::AppConfig& config);
    
    bool importFromFile(const std::string& filePath);
    bool importSingleUrl(const std::string& url);

private:
    sqlite3* db_;
    config::AppConfig config_;
    
    std::string extractRemarksFromUrl(const std::string& url);
    int getNextSortValue();
    bool isUrlExists(const std::string& url);
    bool hasValidPath(const std::string& url);
};

} // namespace update