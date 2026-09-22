#pragma once

#include <string>
#include <optional>
#include <boost/json.hpp>
#include <sqlite3.h>
#include "Profileitem.h"

namespace config {

/// 配置生成模式
enum class ConfigMode {
    Test,       /// 测试配置（单个代理连通性测试）
    Standalone, /// 独立代理配置（单独启动代理）
    Pool        /// 代理池配置（批量代理池）
};

/// 代理后端类型
enum class ProxyType {
    Xray,   /// Xray-core 后端
    SingBox /// Sing-box 后端
};

/// 配置变量（运行时参数）
struct ConfigVariables {
    /// SOCKS 入站端口
    int socksPort = 0;
    
    /// API 端口（仅 Xray 需要）
    int apiPort = 0;
    
    /// 探测 URL（测试模式用）
    std::string probeUrl = "https://www.google.com/generate_204";
    
    /// Observatory 间隔（毫秒，仅 Xray Pool 模式）
    int observatoryInterval = 30000;
    
    /// Balancer 策略（仅 Xray Pool 模式）
    std::string balancerStrategy = "leastPing";
    
    /// 是否启用 Observatory（仅 Xray Pool 模式）
    bool enableObservatory = true;
    
    /// 是否启用 CN/Private 直连分流
    bool enableCnDirect = true;
    
    /// 是否启用 QUIC 封禁
    bool blockQuic = true;
    
    /// 自定义 DNS 配置（可选，覆盖默认值）
    std::optional<boost::json::value> customDns;
    
    /// 自定义路由规则（可选，追加到默认规则）
    std::optional<boost::json::value> customRoutes;
};

/// 配置构建器 - 统一工厂（单点）
/// 三路径 Test/Standalone/Pool × 双后端 Xray/SingBox 单点工厂
class ConfigBuilder {
public:
    /// 构造函数
    /// @param db SQLite 数据库连接（用于读取配置模板）
    explicit ConfigBuilder(sqlite3* db);
    
    /// 析构函数
    ~ConfigBuilder() = default;
    
    /// 检查是否启用新配置构建器（始终返回 true）
    /// @param configPath 已废弃（保留参数以维持 API 兼容性）
    /// @return 始终返回 true - ConfigBuilder 是唯一配置生成路径
    static bool isEnableNewConfigBuilder(const std::string& configPath = "");
    
    /// 主入口：根据模式生成配置（自动使用默认代理客户端）
    /// @param mode 配置模式（Test/Standalone/Pool）
    /// @param profile 代理节点配置
    /// @param vars 运行时变量
    /// @return 完整配置 JSON
    boost::json::value build(
        ConfigMode mode,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    /// 指定客户端生成配置
    /// @param mode 配置模式（Test/Standalone/Pool）
    /// @param clientName 客户端名称（"xray" 或 "singbox"）
    /// @param profile 代理节点配置
    /// @param vars 运行时变量
    /// @return 完整配置 JSON
    boost::json::value build(
        ConfigMode mode,
        const std::string& clientName,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    /// 序列化配置为 JSON 字符串
    /// @param config 配置 JSON 对象
    /// @return JSON 字符串（格式化输出）
    std::string serialize(const boost::json::value& config) const;
    
    /// 序列化配置为 JSON 字符串（紧凑输出）
    /// @param config 配置 JSON 对象
    /// @return JSON 字符串（紧凑输出）
    std::string serializeCompact(const boost::json::value& config) const;
    
    /// 配置深合并（公共 API，用于模板继承）
    /// 规则：对象递归合并，数组/标量覆盖
    /// @param parent 父配置 JSON
    /// @param child 子配置 JSON
    /// @param maxDepth 最大递归深度（防止循环继承）
    /// @return 合并后的配置 JSON
    static boost::json::value mergeConfig(
        const boost::json::value& parent,
        const boost::json::value& child,
        int maxDepth = 10);
    
    /// 从数据库加载模板（按 ID），支持递归继承
    /// @param db SQLite 数据库连接
    /// @param templateId 模板 ID
    /// @param maxDepth 最大递归深度
    /// @return 合并后的配置 JSON
    static boost::json::value loadTemplateById(
        sqlite3* db,
        const std::string& templateId,
        int maxDepth = 10);
    
    /// 校验配置完整性
    /// @param config 配置 JSON 对象
    /// @param mode 配置模式
    /// @param proxyType 代理后端类型
    /// @return True if config is valid, throws std::runtime_error with details if invalid
    bool validateConfig(
        const boost::json::value& config,
        ConfigMode mode,
        ProxyType proxyType) const;

private:
    /// 生成测试模式配置
    boost::json::value buildTestConfig(
        ProxyType proxyType,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    /// 生成独立代理模式配置
    boost::json::value buildStandaloneConfig(
        ProxyType proxyType,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    /// 生成代理池模式配置
    boost::json::value buildPoolConfig(
        ProxyType proxyType,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars);
    
    /// 从 Profileitem 解析代理类型（从 proxy_clients 表读取）
    ProxyType resolveProxyType(const std::string& clientName) const;
    
    /// 获取默认代理客户端类型
    ProxyType getDefaultProxyType() const;
    
    /// 获取代理客户端信息
    /// @param name 客户端名称（"xray" 或 "singbox"）
    /// @return 客户端配置信息
    std::optional<struct ProxyClientInfo> getClientInfo(const std::string& name) const;
    
    /// 加载配置模板
    /// @param templateKey 模板键（如 "test_xray"、"standalone_singbox"）
    /// @return 模板 JSON
    boost::json::value loadTemplate(const std::string& templateKey) const;
    
    /// 替换变量
    /// @param config 配置 JSON
    /// @param vars 变量
    /// @return 替换后的配置 JSON
    boost::json::value replaceVariables(
        const boost::json::value& config,
        const ConfigVariables& vars) const;
    
    /// SQLite 数据库连接
    sqlite3* db_;
};

/// 代理客户端信息
struct ProxyClientInfo {
    /// 客户端名称（"xray" 或 "singbox"）
    std::string name;
    
    /// 可执行文件路径
    std::string executable;
    
    /// 模板目录
    std::string templateDir;
    
    /// API 端口基准
    int apiPortBase = 10115;
    
    /// SOCKS 端口基准
    int socksPortBase = 10808;
    
    /// 资源目录（geoip/geosite）
    std::string assetDir;
    
    /// 是否默认客户端
    bool isDefault = false;
    
    /// 是否启用
    bool enabled = true;
};

} // namespace config