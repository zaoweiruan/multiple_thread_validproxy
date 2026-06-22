#ifndef TEST_PROXY_TEST_COUNTERS_H
#define TEST_PROXY_TEST_COUNTERS_H

#include <atomic>
#include <string>

namespace test {

class ProxyTestCounters {
public:
    ProxyTestCounters();

    void incrementProcessed();
    void incrementSuccess();
    void incrementFailed();
    void incrementProcessed(int delta);

    int getProcessed() const;
    int getSuccess() const;
    int getFailed() const;
    void reset();

    std::string formatSummary(int total) const;

private:
    std::atomic<int> processed_{0};
    std::atomic<int> success_{0};
    std::atomic<int> failed_{0};
};

} // namespace test

#endif // TEST_PROXY_TEST_COUNTERS_H