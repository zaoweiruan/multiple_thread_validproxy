#ifndef POOL_CANDIDATE_H
#define POOL_CANDIDATE_H

#include <string>

namespace pool_candidate {

// 代理池添加代理弹窗候选行（UI 专用 DTO，解耦 Profileitem/ProfileExItem）
struct PoolCandidateItem {
    std::string indexid;
    std::string configtype;
    std::string address;
    std::string remarks;
    std::string region;
    std::string delay;      // ProfileExItem.delay（有效过滤后恒 > 0）
    std::string message;    // ProfileExItem.message（"测试时间+启动时间"）
    int start_count = 0;    // ProfileExItem.start_count
    int crash_count = 0;    // ProfileExItem.crash_count
};

enum class CandidateSortKey {
    IndexId, Protocol, Address, Delay, Region, Health, Message, Remarks
};

// 健康度：贝叶斯平滑 (max(start-crash,0)+1)/(start+2)，start==0 → 0.0
// 与 ProxyListModel::rebuildMaps 公式一致（弹窗场景无运行时加成）
double computeHealth(int start_count, int crash_count);

// 返回 true 表示 a 应排在 b 前（按 key 排序，ascending=true 升序）
// 字符串列大小写不敏感；时延无效值（空/非数字）排后；健康度按数值
bool compareCandidates(const PoolCandidateItem& a, const PoolCandidateItem& b,
                       CandidateSortKey key, bool ascending);

} // namespace pool_candidate

#endif // POOL_CANDIDATE_H