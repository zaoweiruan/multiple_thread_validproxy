#include "AppController.h"
#include "Events.h"

#include "SubitemUpdaterV2.h"
#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "ShareLink.h"
#include "Utils.h"
#include "Logger.h"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/window.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ctime>

// ---------------------------------------------------------------
// AppController implementation
// ---------------------------------------------------------------
AppController::AppController(sqlite3* db, const config::AppConfig& cfg)
    : db_(db), config_(cfg) {}

AppController::~AppController() {
    cancelRequested_ = true;
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

// ---------------------------------------------------------------
// Config
// ---------------------------------------------------------------
bool AppController::saveConfig(const config::AppConfig& cfg) {
    config_ = cfg;
    return true;
}

// ---------------------------------------------------------------
// Subscription operations
// ---------------------------------------------------------------
std::vector<db::models::Subitem> AppController::loadSubscriptions() {
    db::models::SubitemDAO dao(db_);
    return dao.getAll();
}

void AppController::updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doUpdateSubscription, this, subId, wxHandler);
}

void AppController::updateAllSubscriptionsAsync(wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doUpdateAllSubscriptions, this, wxHandler);
}

bool AppController::importSubscription(const std::string& url) {
    try {
        update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "");
        return updater.importSingleUrl(url);
    } catch (const std::exception& e) {
        Logger::write(std::string("Import error: ") + e.what(), LogLevel::ERR);
        return false;
    }
}

// ---------------------------------------------------------------
// Proxy operations
// ---------------------------------------------------------------
std::vector<db::models::Profileitem> AppController::loadProxies(const std::string& subId) {
    db::models::ProfileitemDAO dao(db_);
    if (subId.empty()) {
        return dao.getAll();
    }
    std::vector<db::models::Profileitem> all = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(all.begin(), all.end(), std::back_inserter(filtered),
        [&subId](const db::models::Profileitem& p) { return p.subid == subId; });
    return filtered;
}

std::vector<db::models::ProfileExItem> AppController::loadProxyResults() {
    db::models::ProfileExItemDAO dao(db_);
    return dao.getAll();
}

// ---------------------------------------------------------------
// Test operations
// ---------------------------------------------------------------
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}

// ---------------------------------------------------------------
// Find operations  (sync — kept for CLI path in main.cpp)
// ---------------------------------------------------------------
ProxyFinder::TestResult AppController::findFirstProxy() {
    std::string xrayPath = config_.xray_executable;
    XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findFirstWorkingProxy();
    return finder.getLastResult();
}

ProxyFinder::TestResult AppController::findBestProxy() {
    std::string xrayPath = config_.xray_executable;
    XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findWorkingProxy();
    return finder.getLastResult();
}

// ---------------------------------------------------------------
// Find operations  (async — used by GUI MainFrame)
// ---------------------------------------------------------------
void AppController::findFirstProxyAsync(wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doFindFirstProxy, this, wxHandler);
}

void AppController::findBestProxyAsync(wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doFindBestProxy, this, wxHandler);
}

// ---------------------------------------------------------------
// Export / Tool operations
// ---------------------------------------------------------------
bool AppController::exportShareLinks() {
    db::models::ProfileitemDAO dao(db_);
    db::models::ProfileExItemDAO exDao(db_);

    std::string sql = R"(
        SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
        FROM ProfileItem p
        LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
        WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
        ORDER BY CAST(pe.Delay AS INTEGER) ASC
    )";

    auto profiles = dao.getAll(sql);

    std::string output;
    for (const auto& profile : profiles) {
        auto link = share::ShareLink::toShareUri(
            profile.configtype,
            profile.address,
            profile.port,
            profile.id,
            profile.security,
            profile.network,
            profile.flow,
            profile.sni,
            profile.alpn,
            profile.fingerprint,
            profile.allowinsecure,
            profile.path,
            profile.requesthost,
            profile.headertype,
            profile.streamsecurity,
            profile.remarks,
            profile.echconfiglist,
            profile.publickey,
            profile.shortid
        );
        if (!link.empty()) {
            output += link + "\n";
        }
    }

    if (output.empty()) {
        Logger::write("No proxies to export", LogLevel::INFO);
        return true;
    }

    char timestamp[32];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

    std::filesystem::path outPath = std::filesystem::temp_directory_path() / ("proxies_" + std::string(timestamp) + ".txt");
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream outFile(outPath, std::ios::binary);
    outFile << output;
    outFile.close();

    Logger::write("Exported " + std::to_string(profiles.size()) + " proxies to: " + outPath.string(), LogLevel::INFO);
    return true;
}

bool AppController::deduplicate() {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "");
    return updater.deduplicate();
}

bool AppController::syncDatabases(const std::string& src, const std::string& dst) {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "");
    return updater.syncDatabases(src.empty() ? config_.sync.source_db : src,
                                  dst.empty() ? config_.sync.target_db : dst);
}

bool AppController::generateConfig(const std::string& indexId) {
    try {
        config::ConfigGenerator gen(db_);
        auto profiles = gen.loadProfiles("SELECT * FROM ProfileItem;");
        for (const auto& p : profiles) {
            if (p.indexid == indexId) {
                auto result = gen.generateConfig(p);
                return !result.outbound_json.empty();
            }
        }
        Logger::write("Profile not found: " + indexId, LogLevel::ERR);
    } catch (const std::exception& e) {
        Logger::write(std::string("Generate config error: ") + e.what(), LogLevel::ERR);
    }
    return false;
}

// ---------------------------------------------------------------
// Xray
// ---------------------------------------------------------------
void AppController::stopXray() {
    XrayManager::release();
}

// ---------------------------------------------------------------
// Async workers
// ---------------------------------------------------------------
void AppController::doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "");
    bool ok = updater.runSingle(subId);

    std::string msg = ok ? "Update completed: " + subId : "Update failed: " + subId;
    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
    }
    Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
}

void AppController::doUpdateAllSubscriptions(wxEvtHandler* wxHandler) {
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "");
    bool ok = updater.run();

    std::string msg = ok ? "All subscriptions updated" : "Update (all) had failures";
    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
    }

    Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
}

void AppController::doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    ProxyBatchTester tester(db_, config_, "");
    bool ok = tester.runWithSubId(subId);

    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(2, ok ? "Test completed" : "Test failed"));
    }

    if (ok) {
        // Post to the handler (TestPanel or MainFrame) for UI state updates
        if (wxHandler) {
            wxQueueEvent(wxHandler, new ProxyTestProgressEvent(0, 0, "", "", "", "Batch test finished", true));
        }
        // Broadcast to MainFrame for delay column refresh.
        // Use dynamic_cast to safely get the top-level window from wxEvtHandler.
        // Create a fresh event copy for the MainFrame receiver.
        if (wxWindow* win = dynamic_cast<wxWindow*>(wxHandler)) {
            if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                if (topLevel != win) {
                    wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "", "Batch test finished", true));
                }
            }
        }
    }

    Logger::write(std::string("Batch test ") + (ok ? "succeeded" : "failed"), LogLevel::REPORT);
}

// ---------------------------------------------------------------
// Async find workers
//
// Payload encoding sent via StatusUpdateEvent:
//   "FOUND:<indexId>:<address>"   — working proxy found
//   "NOTFOUND"                    — no working proxy found
//   "ERR:<msg>"                   — exception occurred
// ---------------------------------------------------------------
void AppController::doFindFirstProxy(wxEvtHandler* wxHandler) {
    try {
        std::string xrayPath = config_.xray_executable;
        XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);

        std::pair<int, int> ports = finder.findFirstWorkingProxy();
        auto res = finder.getLastResult();

        if (res.success && ports.first > 0) {
            std::string payload = "FOUND:" + res.indexId + ":" + res.address;
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, payload));
            }
        } else {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND"));
            }
        }

        finder.release();
        XrayManager::release();

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}

void AppController::doFindBestProxy(wxEvtHandler* wxHandler) {
    try {
        std::string xrayPath = config_.xray_executable;
        XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);

        std::pair<int, int> ports = finder.findWorkingProxy();
        auto res = finder.getLastResult();

        if (res.success && ports.first > 0) {
            std::string payload = "FOUND:" + res.indexId + ":" + res.address;
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, payload));
            }
        } else {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND"));
            }
        }

        finder.release();
        XrayManager::release();

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}
