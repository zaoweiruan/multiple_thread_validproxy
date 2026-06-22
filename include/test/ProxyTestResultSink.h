#ifndef TEST_PROXY_TEST_RESULT_SINK_H
#define TEST_PROXY_TEST_RESULT_SINK_H

#include <string>
#include <mutex>
#include <sqlite3.h>
#include "TestResult.h"

namespace test {

class ProxyTestResultSink {
public:
    explicit ProxyTestResultSink(sqlite3* db);

    void recordSuccess(const std::string& indexid, int latencyMs, const std::string& address);
    void recordFailure(const std::string& indexid, const std::string& errorMsg, const std::string& address);
    void recordException(const std::string& indexid, const std::string& errorMsg);

    TestResult getLastResult() const;
    void reset();

private:
    sqlite3* db_;
    mutable std::mutex mutex_;
    TestResult lastResult_;
};

} // namespace test

#endif // TEST_PROXY_TEST_RESULT_SINK_H