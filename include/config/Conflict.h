#pragma once

#include <string>

// windef.h/winerror.h (via wxWidgets headers) define ERROR and WARN macros
// which collide with the config::ConflictSeverity enumeration values below.
// Undefine them at the source so any include order in any TU is safe.
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif
#ifdef IGNORE
#undef IGNORE
#endif

namespace config {

/// 冲突严重级别
enum class ConflictSeverity {
    ERROR,  // 错误，阻止执行
    WARN    // 警告，允许继续
};

/// 冲突处理策略
enum class ConflictStrategy {
    FAIL_FAST,       // 有 ERROR 级别冲突则抛异常
    WARN_CONTINUE,   // 有 WARN 级别冲突则记录日志继续执行
    IGNORE           // 忽略所有冲突
};

/// 冲突类型
enum class ConflictType {
    SINGLETON_CONFLICT,    // 单例组件重复定义
    TAG_CONFLICT,          // 数组组件 tag 重复
    DEPENDENCY_MISSING,    // routing 引用不存在的 inbound/outbound
    INVALID_PARAMS         // 组件参数格式错误
};

/// 冲突信息
struct Conflict {
    ConflictSeverity severity;       // 严重级别
    ConflictType type;               // 冲突类型
    std::string component1;          // 冲突组件1 类型
    std::string component2;          // 冲突组件2 类型（可选）
    std::string message;             // 描述
    int index1;                      // 组件索引
    int index2;                      // 组件索引（可选）
    
    // 来源包信息（用于区分同包 vs 跨包）
    std::string sourceBundle1;       // 组件1 的来源包 ID
    std::string sourceBundle2;       // 组件2 的来源包 ID
    bool isCrossBundle;              // 是否跨包冲突
    
    Conflict() = default;
    Conflict(const Conflict& other) = default;
    Conflict& operator=(const Conflict& other) = default;
    Conflict(Conflict&& other) = default;
    Conflict& operator=(Conflict&& other) = default;
};

} // namespace config
