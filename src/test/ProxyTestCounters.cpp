#include "test/ProxyTestCounters.h"
#include "Logger.h"

namespace test {

void ProxyTestCounters::incrementProcessed() {
    ++processed_;
}

void ProxyTestCounters::incrementSuccess() {
    ++success_;
}

void ProxyTestCounters::incrementFailed() {
    ++failed_;
}

void ProxyTestCounters::incrementProcessed(int delta) {
    processed_ += delta;
}

int ProxyTestCounters::getProcessed() const {
    return processed_;
}

int ProxyTestCounters::getSuccess() const {
    return success_;
}

int ProxyTestCounters::getFailed() const {
    return failed_;
}

void ProxyTestCounters::reset() {
    processed_ = 0;
    success_ = 0;
    failed_ = 0;
}

std::string ProxyTestCounters::formatSummary(int total) const {
    std::string result = "Total: " + std::to_string(total)
        + ", Success: " + std::to_string(success_)
        + ", Failed: " + std::to_string(failed_);
    return result;
}

} // namespace test