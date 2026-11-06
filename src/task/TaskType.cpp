#include "task/TaskType.h"

namespace task {

const char* taskTypeToString(TaskType t) {
    switch (t) {
    case TaskType::NONE:                   return "无任务 (NONE)";
    case TaskType::UPDATE_SUBSCRIPTION:    return "更新订阅 (UPDATE_SUBSCRIPTION)";
    case TaskType::UPDATE_ALL:             return "更新全部订阅 (UPDATE_ALL)";
    case TaskType::TEST_SUBSCRIPTION:      return "测试订阅 (TEST_SUBSCRIPTION)";
    case TaskType::TEST_SINGLE_PROXY:      return "测试单个代理 (TEST_SINGLE_PROXY)";
    case TaskType::TEST_ALL:               return "批量测试 (TEST_ALL)";
    case TaskType::TEST_BATCH:             return "批量测试容器 (TEST_BATCH)";
    case TaskType::TEST_ONLINE:            return "测试在线代理 (TEST_ONLINE)";
    case TaskType::FIND_FIRST:             return "查找首个可用代理 (FIND_FIRST)";
    case TaskType::FIND_BEST:              return "查找最佳代理 (FIND_BEST)";
    case TaskType::FIND_BY_INDEX:          return "按编号查找代理 (FIND_BY_INDEX)";
    case TaskType::DEDUP:                  return "移除重复代理 (DEDUP)";
    case TaskType::SYNC_DB:                return "同步数据库 (SYNC_DB)";
    case TaskType::EXPORT_SHARE_LINKS:     return "导出分享链接 (EXPORT_SHARE_LINKS)";
    case TaskType::RESOLVE_REGION:         return "批量解析地区 (RESOLVE_REGION)";
    case TaskType::RESOLVE_SINGLE_REGION:  return "解析单个地区 (RESOLVE_SINGLE_REGION)";
    case TaskType::GENERATE_CONFIG:        return "生成配置 (GENERATE_CONFIG)";
    case TaskType::AUTO_TASK:              return "自动任务 (AUTO_TASK)";
    case TaskType::PROBE_POOL_MEMBERS:     return "探活池成员 (PROBE_POOL_MEMBERS)";
    case TaskType::PROBE_MEMBER:           return "探活单个池成员 (PROBE_MEMBER)";
    }
    return "未知任务 (UNKNOWN)";
}

} // namespace task
