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
#include <future>
#include <chrono>
#include <thread>
#include <atomic>

// ---------------------------------------------------------------
// AppController implementation
// ---------------------------------------------------------------
AppController::AppController(sqlite3* db, const config::AppConfig& cfg)
    : db_(db), config_(cfg) {}

AppController::~AppController() {
    // Signal cancellation first so any in-flight async work can observe the flag
    cancelRequested_ = true;
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    Logger::write("[AppController] Destructor: cancelRequested_ set to true @ " + std::to_string(ts) + " ms (steady)", LogLevel::DEBUG);

    // Wait for worker thread to finish before releasing XrayManager.
    // WRONG order (release first, detach later) leaves a detached thread
    // running until the OS kills it — the worker may still access XrayManager
    // or xray instances after release() destroys the singleton, causing zombies.
    if (workerThread_.joinable()) {
        // DIAGNOSTIC INSTRUMENTATION (Phase 3/4 test per systematic-debugging skill)
        // Measures real elapsed time from destructor entry to join completion or 5s timeout.
        // REMOVE after hypothesis verification.
        auto joinWaitStart = std::chrono::steady_clock::now();
        Logger::write("[AppController] Destructor: starting 5s join wait for workerThread_", LogLevel::DEBUG);

        // Wait up to 5 seconds for thread to finish gracefully
        // If thread doesn't respond, detach it to allow process exit
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (workerThread_.joinable()) {
                workerThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - joinWaitStart).count();
            Logger::write("[AppController] Destructor: 5s timeout fired after " + std::to_string(elapsedMs) + " ms — detaching", LogLevel::WARN);
            workerThread_.detach();
        } else {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - joinWaitStart).count();
            Logger::write("[AppController] Destructor: worker thread joined successfully after " + std::to_string(elapsedMs) + " ms", LogLevel::DEBUG);
        }
    }

    // Now it is safe to shut down XrayManager — no thread still holds a ref
    XrayManager::release();
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

bool AppController::updateSubscriptionEnabled(const std::string& id, bool enabled) {
    db::models::SubitemDAO dao(db_);
    return dao.updateEnabled(id, enabled);
}

bool AppController::updateSubitem(const db::models::Subitem& sub) {
    db::models::SubitemDAO dao(db_);
    return dao.updateSubitem(sub);
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

std::optional<db::models::Profileitem> AppController::getProxyByIndexId(const std::string& indexId) {
    db::models::ProfileitemDAO dao(db_);
    return dao.getByIndexId(indexId);
}

std::vector<db::models::ProfileExItem> AppController::loadProxyResults() {
    db::models::ProfileExItemDAO dao(db_);
    return dao.getAll();
}

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}

void AppController::testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doTestSingleProxy, this, indexId, wxHandler);
}

void AppController::cancelTest() {
    cancelRequested_ = true;
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    Logger::write("[AppController] cancelTest() called @ " + std::to_string(ts) + " ms (steady), cancelRequested_ set to true", LogLevel::DEBUG);
}

bool AppController::isTestCancelled() const {
    return cancelRequested_.load();
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
    ProxyBatchTester tester(db_, config_, "", &cancelRequested_);
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

void AppController::doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler) {
    ProxyBatchTester tester(db_, config_, "", &cancelRequested_);
    bool ok = tester.runWithIndexId(indexId);

    // Get actual test result (delay + message) from the tester
    auto result = tester.getLastResult();
    std::string delayStr = (result.latencyMs > 0) ? std::to_string(result.latencyMs) : std::string("");
    std::string message   = result.success
                              ? std::to_string(result.latencyMs) + "ms"
                              : result.errorMsg.empty() ? "FAIL" : result.errorMsg;

    // Result row update (indexId+address in remarks col, delay+message in their cols)
    if (wxHandler) {
        wxQueueEvent(wxHandler,
                     new ProxyTestProgressEvent(1, 1, indexId, /*remarks=*/"",
                                                delayStr, message, /*isCompleted=*/true));
    }
    // Status bar update via MainFrame::onStatusUpdate
    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(2, ok ? "Test completed" : "Test failed"));
    }

    Logger::write(std::string("Single proxy test ") + (ok ? "succeeded" : "failed"), LogLevel::REPORT);
}

// ---------------------------------------------------------------
// Async find workers
//
// Payload encoding sent via StatusUpdateEvent:
//   "FOUND:<indexId>:<address>"   — working proxy found
//   "NOTFOUND"                    — no working proxy found
//   "ERR:<msg>"                   — exception occurred
//   "CANCELLED"                   — operation cancelled
// ---------------------------------------------------------------
void AppController::doFindFirstProxy(wxEvtHandler* wxHandler) {
    try {
        if (isTestCancelled()) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }
        std::string xrayPath = config_.xray_executable;
        XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
        if (!manager) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to create XrayManager"));
            }
            return;
        }
        // Start Xray instances if not already running
        if (manager->getInstanceCount() == 0) {
            int started = manager->start(config_.xray_workers, config_.xray_start_port, config_.xray_api_port);
            if (started == 0) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to start Xray instances"));
                }
                return;
            }
        }
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_);

        std::pair<int, int> ports = finder.findFirstWorkingProxy();

        if (isTestCancelled()) {
            XrayManager::release();
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

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

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}

void AppController::doFindBestProxy(wxEvtHandler* wxHandler) {
    try {
        if (isTestCancelled()) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }
        std::string xrayPath = config_.xray_executable;
        XrayManager* manager = XrayManager::getInstance(xrayPath, "", config_.xray_workers);
        if (!manager) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to create XrayManager"));
            }
            return;
        }
        // Start Xray instances if not already running
        if (manager->getInstanceCount() == 0) {
            int started = manager->start(config_.xray_workers, config_.xray_start_port, config_.xray_api_port);
            if (started == 0) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to start Xray instances"));
                }
                return;
            }
        }
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_);

        std::pair<int, int> ports = finder.findWorkingProxy();

        if (isTestCancelled()) {
            XrayManager::release();
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

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

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}

void AppController::findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread([this, indexId, wxHandler]() {
        try {
            auto proxies = loadProxies();
            // Try exact indexId match first
            auto it = std::find_if(proxies.begin(), proxies.end(),
                [&indexId](const auto& p) { return p.indexid == indexId; });
            
            // If not found, try address prefix match
            if (it == proxies.end()) {
                it = std::find_if(proxies.begin(), proxies.end(),
                    [&indexId](const auto& p) { 
                        return p.address.find(indexId) == 0; 
                    });
            }
            
            if (it == proxies.end()) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND:" + indexId));
                }
                return;
            }
            
            ProxyBatchTester tester(db_, config_, "", &cancelRequested_);
            bool ok = tester.runWithIndexId(it->indexid);

            if (wxHandler) {
                auto result = tester.getLastResult();
                std::string delayStr = (result.latencyMs > 0) ? std::to_string(result.latencyMs) : std::string("");
                std::string message = result.success ? std::to_string(result.latencyMs) + "ms"
                                                     : result.errorMsg.empty() ? "FAIL" : result.errorMsg;
                wxQueueEvent(wxHandler,
                             new ProxyTestProgressEvent(1, 1, it->indexid, "", delayStr,
                                                        message, true));
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, ok ? ("FOUND:" + it->indexid + ":" + it->address) : ("ERR:" + message)));
            }

        } catch (const std::exception& e) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
            }
        }
    });
}
