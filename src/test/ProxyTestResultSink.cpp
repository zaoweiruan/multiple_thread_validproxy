#include "test/ProxyTestResultSink.h"
#include "Logger.h"
#include "ProfileExItem.h"
#include <mutex>

namespace test {

ProxyTestResultSink::ProxyTestResultSink(sqlite3* db)
    : db_(db) {
}

void ProxyTestResultSink::recordSuccess(const std::string& indexid, int latencyMs, const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastResult_ = TestResult{};
    lastResult_.success = true;
    lastResult_.latencyMs = latencyMs;
    lastResult_.indexId = indexid;
    lastResult_.address = address;

    db::models::ProfileExItemDAO exDao(db_);
    exDao.updateTestResult(indexid, latencyMs, true, "");

    Logger::write("[ResultSink] Success: " + indexid + " " + address + " " + std::to_string(latencyMs) + "ms", LogLevel::INFO);
}

void ProxyTestResultSink::recordFailure(const std::string& indexid, const std::string& errorMsg, const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastResult_ = TestResult{};
    lastResult_.success = false;
    lastResult_.errorMsg = errorMsg;
    lastResult_.indexId = indexid;
    lastResult_.address = address;

    db::models::ProfileExItemDAO exDao(db_);
    exDao.updateTestResult(indexid, -1, false, errorMsg);

    Logger::write("[ResultSink] Failure: " + indexid + " " + address + " " + errorMsg, LogLevel::INFO);
}

void ProxyTestResultSink::recordException(const std::string& indexid, const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastResult_ = TestResult{};
    lastResult_.success = false;
    lastResult_.errorMsg = errorMsg;
    lastResult_.indexId = indexid;

    db::models::ProfileExItemDAO exDao(db_);
    exDao.updateTestResult(indexid, -1, false, errorMsg);

    Logger::write("[ResultSink] Exception: " + indexid + " " + errorMsg, LogLevel::ERR);
}

TestResult ProxyTestResultSink::getLastResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_;
}

void ProxyTestResultSink::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    lastResult_ = TestResult{};
}

} // namespace test