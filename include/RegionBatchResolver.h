#ifndef REGION_BATCH_RESOLVER_H
#define REGION_BATCH_RESOLVER_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <sqlite3.h>

#include "ConfigReader.h"
#include "Profileitem.h"

class RegionBatchResolver {
public:
    RegionBatchResolver(sqlite3* db, const config::AppConfig& config, std::atomic<bool>* externalCancel = nullptr);
    ~RegionBatchResolver();

    /// Run batch region resolution for all (or filtered subId) valid proxies
    /// that don't have a region yet. Returns number of successfully resolved.
    int run(const std::string& subId = "");

    /// Request cancellation from worker threads
    void cancel();

    /// Set progress callback invoked as (indexId, region, processed, total) after each item.
    /// region == "" means this item did not resolve.
    void setProgressCallback(std::function<void(const std::string&, const std::string&, int, int)> cb) { progressCallback_ = std::move(cb); }

    // Progress reporting (thread-safe)
    int getTotal() const { return totalTargets_; }
    int getProcessed() const { return processedCount_.load(); }
    int getSuccessCount() const { return successCount_.load(); }

    // --- Static helper methods reusable by AppController ---
    /// Query ipinfo.io Lite API for an address. Returns raw CSV response string, empty on failure.
    static std::string fetchRegionFromIpInfo(const std::string& address, const std::string& ipinfoToken);

    /// Extract "country" field from ipinfo.io JSON response.
    static std::string parseRegionFromJson(const std::string& jsonStr);

private:
    struct ResolveTarget {
        std::string indexId;
        std::string address;
        std::string remarks;
        db::models::Profileitem profile;  // Full profile for config generation
    };

    int calculateWorkerCount(int proxyCount) const;
    std::vector<ResolveTarget> loadTargets(const std::string& subId);
    void workerThreadFunc(int workerId);
    void flushRegionBuffer();

    sqlite3* db_;
    config::AppConfig config_;

    std::vector<ResolveTarget> targets_;
    std::queue<int> workQueue_;
    mutable std::mutex queueMutex_;

    std::atomic<int> processedCount_{0};
    std::atomic<int> successCount_{0};
    std::atomic<int> emptyResponseCount_{0};   // fetch returned empty (DNS/curl failure)
    std::atomic<int> emptyRegionCount_{0};     // region parsed empty
    std::atomic<int> errorCount_{0};           // exception occurred
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool>* externalCancel_{nullptr};
    std::function<void(const std::string&, const std::string&, int, int)> progressCallback_;
    int totalTargets_{0};

    // Batch DB update buffer
    std::vector<std::pair<std::string, std::string>> regionBuffer_;
    std::mutex bufferMutex_;
    static constexpr int BATCH_FLUSH_SIZE = 50;
};

#endif // REGION_BATCH_RESOLVER_H
