#ifndef SERVICE_SHARELINK_EXPORT_SERVICE_H
#define SERVICE_SHARELINK_EXPORT_SERVICE_H

#include <string>
#include <tuple>
#include <sqlite3.h>

namespace service {

class ShareLinkExportService {
public:
    ShareLinkExportService(sqlite3* db);

    std::tuple<bool, int, std::string> exportShareLinks();

private:
    sqlite3* db_;

    std::string generateExportFilename() const;
};

} // namespace service

#endif // SERVICE_SHARELINK_EXPORT_SERVICE_H