#include "PoolCandidate.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pool_candidate {

double computeHealth(int start_count, int crash_count) {
    if (start_count <= 0) return 0.0;
    int stable = start_count - crash_count;
    if (stable < 0) stable = 0;
    return static_cast<double>(stable + 1) / static_cast<double>(start_count + 2);
}

namespace {

int parseDelay(const std::string& s) {
    if (s.empty()) return -1;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return -1;  // 无数字前缀
    return static_cast<int>(v);
}

int compareString(const std::string& a, const std::string& b) {
    std::string la = a;
    std::string lb = b;
    std::transform(la.begin(), la.end(), la.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lb.begin(), lb.end(), lb.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}

int compareValues(const PoolCandidateItem& a, const PoolCandidateItem& b,
                  CandidateSortKey key) {
    switch (key) {
        case CandidateSortKey::IndexId:
            return compareString(a.indexid, b.indexid);
        case CandidateSortKey::Protocol:
            return compareString(utils::getProtocolName(a.configtype),
                                 utils::getProtocolName(b.configtype));
        case CandidateSortKey::Address:
            return compareString(a.address, b.address);
        case CandidateSortKey::Delay: {
            const int da = parseDelay(a.delay);
            const int db = parseDelay(b.delay);
            if (da < 0 && db < 0) return 0;
            if (da < 0) return 1;   // 无效排后
            if (db < 0) return -1;
            return (da < db) ? -1 : (da > db) ? 1 : 0;
        }
        case CandidateSortKey::Region:
            return compareString(a.region, b.region);
        case CandidateSortKey::Health: {
            const double ha = computeHealth(a.start_count, a.crash_count);
            const double hb = computeHealth(b.start_count, b.crash_count);
            if (ha < hb) return -1;
            if (ha > hb) return 1;
            return 0;
        }
        case CandidateSortKey::Message:
            return compareString(a.message, b.message);
        case CandidateSortKey::Remarks:
            return compareString(a.remarks, b.remarks);
    }
    return 0;
}

} // namespace

bool compareCandidates(const PoolCandidateItem& a, const PoolCandidateItem& b,
                       CandidateSortKey key, bool ascending) {
    const int cmp = compareValues(a, b, key);
    if (cmp == 0) return false;
    return ascending ? (cmp < 0) : (cmp > 0);
}

} // namespace pool_candidate