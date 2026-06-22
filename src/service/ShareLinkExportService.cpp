#include "service/ShareLinkExportService.h"
#include "ShareLink.h"
#include "Profileitem.h"
#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <ctime>
#include "Utils.h"

namespace service {

ShareLinkExportService::ShareLinkExportService(sqlite3* db)
    : db_(db) {
}

std::string ShareLinkExportService::generateExportFilename() const {
    char timestamp[32];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
    return std::string("proxies_") + timestamp + ".txt";
}

std::tuple<bool, int, std::string> ShareLinkExportService::exportShareLinks() {
    db::models::ProfileitemDAO dao(db_);

    std::string sql = "SELECT p.* FROM ProfileItem p LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0";
    std::vector<db::models::Profileitem> profiles = dao.getAll(sql);

    std::string output;
    int exportCount = 0;
    for (const db::models::Profileitem& profile : profiles) {
        std::string link = share::ShareLink::toShareUri(
            profile.configtype, profile.address, profile.port, profile.id,
            profile.security, profile.network, profile.flow, profile.sni,
            profile.alpn, profile.fingerprint, profile.allowinsecure,
            profile.path, profile.requesthost, profile.headertype,
            profile.streamsecurity, profile.remarks,
            profile.echconfiglist, profile.publickey, profile.shortid
        );
        if (!link.empty()) {
            output += link + "\n";
            ++exportCount;
        }
    }

    if (output.empty()) {
        return {true, 0, ""};
    }

    std::filesystem::path outPath = std::filesystem::path(utils::getExecutableDir()) / "proxies" / generateExportFilename();
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream outFile(outPath, std::ios::binary);
    outFile << output;
    outFile.close();

    Logger::write("Exported " + std::to_string(exportCount) + " proxies to: " + outPath.string(), LogLevel::INFO);
    return {true, exportCount, outPath.filename().string()};
}

} // namespace service