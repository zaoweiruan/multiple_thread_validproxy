#include "task/TaskType.h"
#include "i18n/I18n.h"

namespace task {

const char* taskTypeToString(TaskType t) {
    switch (t) {
    case TaskType::NONE:                   return i18n::u8(i18n::StrId::TaskType_None);
    case TaskType::UPDATE_SUBSCRIPTION:    return i18n::u8(i18n::StrId::TaskType_UpdateSubscription);
    case TaskType::UPDATE_ALL:             return i18n::u8(i18n::StrId::TaskType_UpdateAll);
    case TaskType::TEST_SUBSCRIPTION:      return i18n::u8(i18n::StrId::TaskType_TestSubscription);
    case TaskType::TEST_SINGLE_PROXY:      return i18n::u8(i18n::StrId::TaskType_TestSingleProxy);
    case TaskType::TEST_ALL:               return i18n::u8(i18n::StrId::TaskType_TestAll);
    case TaskType::TEST_BATCH:             return i18n::u8(i18n::StrId::TaskType_TestBatch);
    case TaskType::TEST_ONLINE:            return i18n::u8(i18n::StrId::TaskType_TestOnline);
    case TaskType::FIND_FIRST:             return i18n::u8(i18n::StrId::TaskType_FindFirst);
    case TaskType::FIND_BEST:              return i18n::u8(i18n::StrId::TaskType_FindBest);
    case TaskType::FIND_BY_INDEX:          return i18n::u8(i18n::StrId::TaskType_FindByIndex);
    case TaskType::DEDUP:                  return i18n::u8(i18n::StrId::TaskType_Dedup);
    case TaskType::SYNC_DB:                return i18n::u8(i18n::StrId::TaskType_SyncDb);
    case TaskType::EXPORT_SHARE_LINKS:     return i18n::u8(i18n::StrId::TaskType_ExportShareLinks);
    case TaskType::RESOLVE_REGION:         return i18n::u8(i18n::StrId::TaskType_ResolveRegion);
    case TaskType::RESOLVE_SINGLE_REGION:  return i18n::u8(i18n::StrId::TaskType_ResolveSingleRegion);
    case TaskType::GENERATE_CONFIG:        return i18n::u8(i18n::StrId::TaskType_GenerateConfig);
    case TaskType::AUTO_TASK:              return i18n::u8(i18n::StrId::TaskType_AutoTask);
    case TaskType::PROBE_POOL_MEMBERS:     return i18n::u8(i18n::StrId::TaskType_ProbePoolMembers);
    case TaskType::PROBE_MEMBER:           return i18n::u8(i18n::StrId::TaskType_ProbeMember);
    }
    return i18n::u8(i18n::StrId::TaskType_Unknown);
}

} // namespace task
