#include "RegionBatchResolver.h"
#include "CurlEasyHandle.h"
#include "ConfigGenerator.h"
#include "Profileitem.h"
#include "Logger.h"
#include "utils/DnsCache.h"

#include <algorithm>
#include <thread>
#include <vector>
#include <boost/json.hpp>

// ---------------------------------------------------------------
RegionBatchResolver::RegionBatchResolver(sqlite3* db, const config::AppConfig& config,
                                         std::atomic<bool>* externalCancel)
    : db_(db), config_(config), externalCancel_(externalCancel) {
}

RegionBatchResolver::~RegionBatchResolver() {
    cancel();
}

// ---------------------------------------------------------------
int RegionBatchResolver::calculateWorkerCount(int proxyCount) const {
    return std::min(proxyCount, config_.xray_workers);
}

// ---------------------------------------------------------------
std::vector<RegionBatchResolver::ResolveTarget> RegionBatchResolver::loadTargets(const std::string& subId) {
    std::vector<ResolveTarget> targets;

    // Load full profiles via ConfigGenerator::loadProfiles with filtered query
    config::ConfigGenerator configGen(db_);

    std::string subFilter;
    if (!subId.empty()) {
        subFilter = " AND p.SubId = '" + subId + "'";
    }

    std::string sql =
        "SELECT DISTINCT p.* FROM ProfileItem p"
        " INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId"
        " WHERE CAST(e.delay AS INTEGER) > 0" + subFilter;

    std::vector<db::models::Profileitem> profiles = configGen.loadProfiles(sql);

    targets.reserve(profiles.size());
    for (const auto& profile : profiles) {
        targets.push_back({profile.indexid, profile.address, profile.remarks, profile});
    }

    Logger::write("[RegionBatchResolver] loadTargets: found " + std::to_string(targets.size()) + " proxies for region resolution", LogLevel::INFO);
    return targets;
}

// ---------------------------------------------------------------
int RegionBatchResolver::run(const std::string& subId) {
    // 1. Load targets
    targets_ = loadTargets(subId);
    if (targets_.empty()) {
        Logger::write("[RegionBatchResolver] No targets to resolve", LogLevel::INFO);
        return 0;
    }

    totalTargets_ = static_cast<int>(targets_.size());
    processedCount_ = 0;
    successCount_ = 0;
    cancelRequested_ = false;

    // 2. Calculate worker count
    int numWorkers = calculateWorkerCount(totalTargets_);
    Logger::write("[RegionBatchResolver] Starting with " + std::to_string(numWorkers)
                  + " workers for " + std::to_string(totalTargets_) + " targets", LogLevel::INFO);

    // 3. Fill work queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (int i = 0; i < totalTargets_; ++i) {
            workQueue_.push(i);
        }
    }

    // 4. Launch worker threads (no Xray needed, direct curl to ipinfo.io)
    std::vector<std::thread> workers;
    workers.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers.emplace_back(&RegionBatchResolver::workerThreadFunc, this, i);
    }

    // 4b. Progress reporter thread — logs current/total every 500ms (starting from 1)
    std::atomic<bool> reporterStop{false};
    std::thread progressReporter([&]() {
        while (!reporterStop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            int proc = processedCount_.load();
            int display = proc + 1;  // 1-based index
            if (display > totalTargets_) break;
            Logger::write("[RegionBatchResolver] Progress: " + std::to_string(display)
                          + " / " + std::to_string(totalTargets_), LogLevel::INFO);
        }
    });

    // 5. Join all workers
    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    reporterStop.store(true);
    if (progressReporter.joinable()) {
        progressReporter.join();
    }

    // 6. Flush remaining buffer
    flushRegionBuffer();

    // 7. Final report with failure breakdown
    int failEmptyResp = emptyResponseCount_.load();
    int failEmptyRegion = emptyRegionCount_.load();
    int failError = errorCount_.load();
    int totalFail = failEmptyResp + failEmptyRegion + failError;
    std::string reasonParts;
    if (failEmptyResp > 0) reasonParts += "fetch_empty=" + std::to_string(failEmptyResp);
    if (failEmptyRegion > 0) {
        if (!reasonParts.empty()) reasonParts += ",";
        reasonParts += "parse_empty=" + std::to_string(failEmptyRegion);
    }
    if (failError > 0) {
        if (!reasonParts.empty()) reasonParts += ",";
        reasonParts += "exception=" + std::to_string(failError);
    }
    std::string finalMsg = "[RegionBatchResolver] Completed: " + std::to_string(successCount_.load())
                   + " / " + std::to_string(totalTargets_) + " resolved";
    if (totalFail > 0) {
        finalMsg += " (FAILED=" + std::to_string(totalFail) + " [" + reasonParts + "])";
    }
    Logger::write(finalMsg, LogLevel::REPORT);
    return successCount_.load();
}

// ---------------------------------------------------------------
void RegionBatchResolver::cancel() {
    cancelRequested_ = true;
}

// ---------------------------------------------------------------
void RegionBatchResolver::workerThreadFunc(int workerId) {
    while (true) {
        if (cancelRequested_.load() || (externalCancel_ && externalCancel_->load())) {
            Logger::write("[RegionBatchResolver] Worker " + std::to_string(workerId)
                          + " exiting (cancelled)", LogLevel::DEBUG);
            break;
        }

        int targetIdx = -1;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (workQueue_.empty()) {
                Logger::write("[RegionBatchResolver] Worker " + std::to_string(workerId)
                              + " exiting (queue empty)", LogLevel::DEBUG);
                break;
            }
            targetIdx = workQueue_.front();
            workQueue_.pop();
        }

        if (targetIdx < 0 || targetIdx >= static_cast<int>(targets_.size())) {
            processedCount_++;
            if (progressCallback_) {
                progressCallback_("", "", processedCount_.load(), totalTargets_);
            }
            continue;
        }

        const ResolveTarget& target = targets_[targetIdx];

        int curSeq = processedCount_.load() + 1;  // 1-based
        Logger::write("[RegionBatchResolver] [" + std::to_string(curSeq) + "/" + std::to_string(totalTargets_) + "] Resolving region for "
                      + target.address + " (" + target.indexId + ")", LogLevel::INFO);

        try {
            // Direct curl to ipinfo.io (no Xray SOCKS5 proxy needed)
            std::string response = fetchRegionFromIpInfo(target.address, config_.ipinfo_token);
            std::string region;
            if (!response.empty()) {
                region = parseRegionFromJson(response);
            }

            if (response.empty()) {
                emptyResponseCount_++;
                Logger::write("[RegionBatchResolver] fetchRegionFromIpInfo returned empty for "
                              + target.address + " (" + target.indexId + ")", LogLevel::INFO);
                processedCount_++;
                if (progressCallback_) {
                    progressCallback_(target.indexId, "", processedCount_.load(), totalTargets_);
                }
                continue;
            }

            if (region.empty()) {
                emptyRegionCount_++;
                Logger::write("[RegionBatchResolver] Worker " + std::to_string(workerId)
                              + " empty region for " + target.indexId + " (skipped)", LogLevel::INFO);
                processedCount_++;
                if (progressCallback_) {
                    progressCallback_(target.indexId, "", processedCount_.load(), totalTargets_);
                }
                continue;
            }

            // Buffer the result for batch DB update
            // NOTE: swap buffer under lock, but flush (SQLite) OUTSIDE the lock
            // to prevent one worker blocking others during DB I/O
            bool needFlush = false;
            {
                std::lock_guard<std::mutex> lock(bufferMutex_);
                regionBuffer_.emplace_back(target.indexId, region);
                if (static_cast<int>(regionBuffer_.size()) >= BATCH_FLUSH_SIZE) {
                    needFlush = true;
                }
            }
            if (needFlush) {
                flushRegionBuffer();
            }

            Logger::write("[RegionBatchResolver] " + target.address + " (" + target.indexId + ") -> " + region, LogLevel::INFO);

            successCount_++;
            processedCount_++;
            if (progressCallback_) {
                progressCallback_(target.indexId, region, processedCount_.load(), totalTargets_);
            }

        } catch (const std::exception& e) {
            Logger::write("[RegionBatchResolver] Worker " + std::to_string(workerId)
                          + " error for " + target.indexId + ": " + e.what(), LogLevel::WARN);
            errorCount_++;
            processedCount_++;
            if (progressCallback_) {
                progressCallback_(target.indexId, "", processedCount_.load(), totalTargets_);
            }
        }
    }
}

// ---------------------------------------------------------------
// Static helper: query ipinfo.io Lite API (returns JSON)
// ---------------------------------------------------------------
std::string RegionBatchResolver::fetchRegionFromIpInfo(const std::string& address, const std::string& ipinfoToken) {
    std::string responseBody;

    try {
        // DNS resolution: if address is a domain (not raw IP), resolve it first
        std::string targetAddress = address;

        // Inline IP check: true for IPv4 (3 dots, all digits/dots) or IPv6 (contains ':')
        auto isIpPattern = [](const std::string& s) -> bool {
            if (std::all_of(s.begin(), s.end(), [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
            })) {
                size_t dotCount = 0;
                for (char c : s) if (c == '.') dotCount++;
                if (dotCount == 3) return true;
            }
            if (s.find(':') != std::string::npos) return true;
            return false;
        };

        if (!isIpPattern(address)) {
            Logger::write("[RegionBatchResolver] Address " + address + " is a domain, resolving DNS...", LogLevel::INFO);
            std::string resolvedIp = utils::DnsCache::resolve(address);
            if (resolvedIp.empty()) {
                Logger::write("[RegionBatchResolver] DNS resolution failed for " + address + ", skipping", LogLevel::INFO);
                return "";
            }
            Logger::write("[RegionBatchResolver] DNS resolved " + address + " -> " + resolvedIp, LogLevel::INFO);
            targetAddress = resolvedIp;
        }

        CurlEasyHandle curl;

        // Direct curl to ipinfo.io Lite API (no SOCKS5 proxy needed)
        std::string url = "https://api.ipinfo.io/lite/" + targetAddress + "?token=" + ipinfoToken;
        curl.setUrl(url);
        curl.setTimeoutSec(15);
        curl.setConnectTimeoutMs(10000);
        curl.setSslVerifyPeer(false);
        curl.setSslVerifyHost(false);
        curl.setFollowLocation(true);

        // Capture response body
        curl.setWriteCallback(CurlEasyHandle::writeCallback, &responseBody);

        curl.perform();
    } catch (const std::exception& e) {
        Logger::write("[RegionBatchResolver] curl request failed: " + std::string(e.what()), LogLevel::TRACE);
        return "";
    }

    // Log the raw API response at DEBUG level
    if (responseBody.empty()) {
        Logger::write("[RegionBatchResolver] API returned empty response", LogLevel::INFO);
    } else {
        Logger::write("[RegionBatchResolver] API response: " + responseBody, LogLevel::DEBUG);
    }

    return responseBody;
}

// ---------------------------------------------------------------
// Static helper: parse JSON response from ipinfo.io Lite API
// Format: {"ip": "...", "country": "United States", "country_code": "US", ...}
// Returns the full country name (e.g., "United States"), uppercased.
// ---------------------------------------------------------------
std::string RegionBatchResolver::parseRegionFromJson(const std::string& jsonStr) {
    try {
        if (jsonStr.empty()) {
            Logger::write("[RegionBatchResolver] parseRegionFromJson: empty JSON string", LogLevel::INFO);
            return "";
        }

        // Parse JSON
        boost::json::value parsed = boost::json::parse(jsonStr);
        const boost::json::object& obj = parsed.as_object();

        // Extract "country" field
        auto it = obj.find("country");
        if (it == obj.end()) {
            Logger::write("[RegionBatchResolver] parseRegionFromJson: no 'country' field in JSON", LogLevel::INFO);
            return "";
        }

        std::string country = it->value().as_string().c_str();

        // Trim whitespace and newline
        country.erase(0, country.find_first_not_of(" \t\r\n"));
        country.erase(country.find_last_not_of(" \t\r\n") + 1);

        if (country.empty()) {
            Logger::write("[RegionBatchResolver] parseRegionFromJson: country field is empty", LogLevel::INFO);
            return "";
        }

        // Uppercase the country code (ipinfo.io may return mixed case)
        std::transform(country.begin(), country.end(), country.begin(), ::toupper);

        return country;
    } catch (const std::exception& e) {
        Logger::write("[RegionBatchResolver] JSON parse error: " + std::string(e.what()), LogLevel::INFO);
        return "";
    }
}

// ---------------------------------------------------------------
void RegionBatchResolver::flushRegionBuffer() {
    std::vector<std::pair<std::string, std::string>> batch;
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        if (regionBuffer_.empty()) return;  // safe: mutex held
        batch.swap(regionBuffer_);
    }

    if (batch.empty()) return;

    const char* updateSql = "UPDATE ProfileItem SET Region = ? WHERE IndexId = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("[RegionBatchResolver] flushBuffer: prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("[RegionBatchResolver] flushBuffer: BEGIN failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_finalize(stmt);
        return;
    }

    bool commitOk = true;
    for (const auto& entry : batch) {
        sqlite3_bind_text(stmt, 1, entry.second.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entry.first.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::write("[RegionBatchResolver] flushBuffer: step error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            commitOk = false;
            sqlite3_reset(stmt);
            break;
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    if (commitOk) {
        if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            Logger::write("[RegionBatchResolver] flushBuffer: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
            sqlite3_free(errMsg);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    Logger::write("[RegionBatchResolver] flushBuffer: wrote " + std::to_string(batch.size()) + " regions, remaining in buffer: "
                  + std::to_string(regionBuffer_.size()), LogLevel::INFO);
}
