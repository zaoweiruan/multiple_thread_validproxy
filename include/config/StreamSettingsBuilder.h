#ifndef STREAM_SETTINGS_BUILDER_H
#define STREAM_SETTINGS_BUILDER_H

#include <string>
#include <boost/json.hpp>
#include "Profileitem.h"

namespace config {

/// ECH 开关（proxy.ech_enabled），由 AppController / main_cli 在启动时下发一次。
/// 默认 false —— 不输出 echConfigList（安全侧：维持「未启用即不下发」的既有语义）。
/// 做成进程级开关而非 build() 参数，是为了不改 5 处调用点
/// （SS/Trojan/VLESS/VMess 四个 outbound builder）。
void setEchEnabled(bool enabled);

/// 读取当前 ECH 开关。默认 false。
bool echEnabled();

class StreamSettingsBuilder {
public:
    boost::json::object build(const db::models::Profileitem& profile) const;
};

} // namespace config

#endif // STREAM_SETTINGS_BUILDER_H
