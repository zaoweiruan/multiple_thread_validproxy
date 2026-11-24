#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <atomic>
#include "ConfigReader.h"
#include "task/TaskType.h"

namespace update {

class Deduplicator {
public:
    Deduplicator(sqlite3* db, const config::AppConfig& config);
    
    // stats (optional): filled at the Deduplication Summary with the existing
    // count variables — total=before, success=after, failed=config-error,
    // skipped=total-success-failed (derived, so success+failed+skipped==total).
    // nullptr (default) keeps the current behaviour exactly.
    bool deduplicate(task::TaskStats* stats = nullptr);
    
    int getProtectedCount() const { return protectedCount_; }
    int getBlacklistedCount() const { return blacklistedCount_; }
    int getInvalidCount() const { return invalidCount_; }
    int getMergedCount() const { return mergedCount_; }
    int getConfigErrorCount() const { return configErrorCount_; }
    int getBeforeCount() const { return beforeCount_; }
    int getAfterCount() const { return afterCount_; }

private:
    sqlite3* db_;
    config::AppConfig config_;
    
    int beforeCount_ = 0;
    int afterCount_ = 0;
    int protectedCount_ = 0;
    int blacklistedCount_ = 0;
    int invalidCount_ = 0;
    int mergedCount_ = 0;
    int configErrorCount_ = 0;
    
    int countAllProxies();
    int deduplicatePhase0();
    int deduplicatePhase1();
    int deduplicateMergedPhase();
    int deduplicateBlacklistPhase();
    int deduplicateConfigErrorPhase();
    void cleanupProfileExItem();
};

} // namespace update