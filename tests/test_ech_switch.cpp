// ECH 开关测试（Spec EchSwitch，2026-10-14）
// 覆盖：默认关闭 / 关闭时不输出 echConfigList / 开启时空格替换为 '+' / 无空格原样输出
#include <gtest/gtest.h>

#include "Profileitem.h"
#include "config/StreamSettingsBuilder.h"
#include "ConfigReader.h"

namespace {

db::models::Profileitem makeTlsProfile(const std::string& echList) {
    db::models::Profileitem p;
    p.network = "tcp";
    p.streamsecurity = "tls";
    p.address = "example.com";
    p.port = "443";
    p.echconfiglist = echList;
    return p;
}

// 从 streamSettings 里取出 tls.echConfigList；不存在返回 nullopt 语义的空串 + found 标志
bool extractEch(const boost::json::object& stream,
                std::string& value,
                bool& found) {
    found = false;
    if (!stream.contains("tlsSettings")) {
        return false;
    }
    const boost::json::object& tls = stream.at("tlsSettings").as_object();
    if (!tls.contains("echConfigList")) {
        return false;
    }
    found = true;
    value = std::string(tls.at("echConfigList").as_string().c_str());
    return true;
}

class EchSwitchTest : public ::testing::Test {
protected:
    void TearDown() override {
        // 复位进程级开关，避免用例间泄漏
        config::setEchEnabled(false);
    }
};

} // namespace

TEST_F(EchSwitchTest, DefaultDisabledOmitsEchConfigList) {
    // 规格：默认 false —— 未显式下发时不应输出 echConfigList
    config::setEchEnabled(false);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile("abcd")), v, found);
    EXPECT_FALSE(found) << "ECH 关闭时不应输出 echConfigList";
}

TEST_F(EchSwitchTest, DisabledOmitsEchConfigListEvenWhenProfileHasValue) {
    // 规格：false → EchConfigList 字段为空（即不下发），即使 profile 里有值
    config::setEchEnabled(false);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile("abcd wxyz")), v, found);
    EXPECT_FALSE(found);
}

TEST_F(EchSwitchTest, EnabledReplacesSpacesWithPlus) {
    // 规格：true → 用 '+' 替换其中的空格
    config::setEchEnabled(true);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile("ab cd ef")), v, found);
    ASSERT_TRUE(found);
    EXPECT_EQ("ab+cd+ef", v);
}

TEST_F(EchSwitchTest, EnabledLeadingAndTrailingSpacesReplaced) {
    config::setEchEnabled(true);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile(" ab cd ")), v, found);
    ASSERT_TRUE(found);
    EXPECT_EQ("+ab+cd+", v);
}

TEST_F(EchSwitchTest, EnabledNoSpacesUnchanged) {
    // 无空格时原样输出，不做任何改写
    config::setEchEnabled(true);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile("abcd+/ef")), v, found);
    ASSERT_TRUE(found);
    EXPECT_EQ("abcd+/ef", v);
}

TEST_F(EchSwitchTest, EnabledEmptyListStillOmitted) {
    // 空值时不输出该字段（保持既有 empty 判断）
    config::setEchEnabled(true);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(makeTlsProfile("")), v, found);
    EXPECT_FALSE(found);
}

TEST_F(EchSwitchTest, ConfigItemDefaultsToFalse) {
    // 配置项默认值必须为 false（规格明确）
    config::AppConfig cfg;
    EXPECT_FALSE(cfg.proxy.ech_enabled);
}

TEST_F(EchSwitchTest, SetterRoundTrips) {
    config::setEchEnabled(true);
    EXPECT_TRUE(config::echEnabled());
    config::setEchEnabled(false);
    EXPECT_FALSE(config::echEnabled());
}

// 回归（2026-10-14）：用户报告「启用 ECH 后生成的配置里没有 echConfigList」。
// 根因**不是**生成逻辑，而是开关的生效时机：
//   setEchEnabled() 原本只在 ConfigReader::load() 末尾下发，即**仅进程启动时**。
//   用户在配置窗口勾选 ECH 后，值只写进磁盘与对话框内存副本，
//   不会重新触发 load()，于是紧接着生成的配置仍用旧值 false。
//   时间线佐证：standalone 配置生成于 14:15:41，而 config.json 写入 ech_enabled=true
//   在 14:17:29 —— 产物早于开关 1 分 48 秒。
// 修复=AppController::saveConfig() 里补一次下发（与同函数内既有的
//   utils::setTimeWaitBlocking 同一模式）。
// 本用例锁定「保存路径会重新下发」这一契约。
TEST_F(EchSwitchTest, SavePathRepublishesSwitch) {
    // 模拟：启动时为 false，随后配置窗口保存为 true 并重新下发
    config::setEchEnabled(false);
    ASSERT_FALSE(config::echEnabled());

    // AppController::saveConfig 的行为：config_ = cfg; 然后 setEchEnabled(config_.proxy.ech_enabled)
    config::AppConfig edited;
    edited.proxy.ech_enabled = true;
    config::setEchEnabled(edited.proxy.ech_enabled);
    EXPECT_TRUE(config::echEnabled());

    // 此时生成的配置必须含 echConfigList
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(
                   makeTlsProfile("cloudflare-ech.com udp://1.1.1.1")), v, found);
    ASSERT_TRUE(found) << "保存后立即生成配置应带 echConfigList";
    EXPECT_EQ("cloudflare-ech.com+udp://1.1.1.1", v);
}

// 用真实故障节点的完整字段组合锁定 tls 分支必然产出该字段。
TEST_F(EchSwitchTest, RealReportedNodeFieldsProduceEchConfigList) {
    // IndexId=4954911979331537460 现场：ws + tls(sni=wallweb-av6.pages.dev, fp=chrome)
    //       echconfiglist='cloudflare-ech.com udp://1.1.1.1'
    db::models::Profileitem p;
    p.network = "ws";
    p.streamsecurity = "tls";
    p.address = "www.speedtest.net";
    p.port = "443";
    p.sni = "wallweb-av6.pages.dev";
    p.fingerprint = "chrome";
    p.allowinsecure = "0";
    p.echconfiglist = "cloudflare-ech.com udp://1.1.1.1";

    config::setEchEnabled(true);
    std::string v; bool found = false;
    extractEch(config::StreamSettingsBuilder().build(p), v, found);
    ASSERT_TRUE(found) << "tls 分支应产出 echConfigList";
    EXPECT_EQ("cloudflare-ech.com+udp://1.1.1.1", v);

    // 关闭开关后同一节点不再产出该字段
    config::setEchEnabled(false);
    extractEch(config::StreamSettingsBuilder().build(p), v, found);
    EXPECT_FALSE(found);
}