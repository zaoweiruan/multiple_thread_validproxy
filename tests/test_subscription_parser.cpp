#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "Logger.h"
#include "update/SubscriptionParser.h"

// ============================================================
// SubscriptionParser Tests
// ============================================================

TEST(SubscriptionParserTest, ParseVlessBasic) {
    update::SubscriptionParser parser;
    
    std::string content = "vless://uuid123@example.com:443?type=tcp";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "5");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
    EXPECT_EQ(profiles[0].id, "uuid123");
    EXPECT_EQ(profiles[0].subid, "test-sub");
}

TEST(SubscriptionParserTest, ParseVlessWithRemarks) {
    update::SubscriptionParser parser;
    
    std::string content = "vless://uuid123@example.com:443?type=tcp#My%20Server";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "5");
    EXPECT_EQ(profiles[0].remarks, "My Server");
}

TEST(SubscriptionParserTest, ParseShadowSocksBasic) {
    update::SubscriptionParser parser;
    
    // SS format: ss://base64(method:password)@host:port
    // aes-256-gcm:password123 -> YXNlcy0yNTYtZ2NtOnBhc3N3b3JkMTIz
    std::string content = "ss://YmFzZTY0LWtleTpwYXNzd29yZA@example.com:8388";  // Simplified test - just check configtype
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "3");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "8388");
}

TEST(SubscriptionParserTest, ParseTrojanBasic) {
    update::SubscriptionParser parser;
    
    std::string content = "trojan://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "6");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
    EXPECT_EQ(profiles[0].id, "password123");
}

TEST(SubscriptionParserTest, ParseHysteria2Basic) {
    update::SubscriptionParser parser;
    
    std::string content = "hysteria2://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "7");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
}

TEST(SubscriptionParserTest, ParseHy2Alias) {
    update::SubscriptionParser parser;
    
    std::string content = "hy2://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "7");
}

TEST(SubscriptionParserTest, ParseMixedProtocols) {
    update::SubscriptionParser parser;
    
    std::string content = 
        "vless://uuid1@example.com:443?type=tcp\r\n"
        "trojan://pass@example.com:443\r\n"
        "hy2://pass@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 3);
    EXPECT_EQ(profiles[0].configtype, "5");  // vless
    EXPECT_EQ(profiles[1].configtype, "6");  // trojan
    EXPECT_EQ(profiles[2].configtype, "7");  // hysteria2
}

TEST(SubscriptionParserTest, ParseInvalidContentReturnsEmpty) {
    update::SubscriptionParser parser;
    
    std::string content = "invalid-content-without-protocol";
    
    auto profiles = parser.parse(content, "test-sub");
    
    EXPECT_EQ(profiles.size(), 0);
}

TEST(SubscriptionParserTest, ParseEmptyContentReturnsEmpty) {
    update::SubscriptionParser parser;
    
    std::string content = "";
    
    auto profiles = parser.parse(content, "test-sub");
    
    EXPECT_EQ(profiles.size(), 0);
}
// ============================================================
// P0 可观测性测试（依据 docs/specs/2026-10-15-Spec-SubscriptionBase64UrlFix-v1.2.md §3.1）
//
// 覆盖独立评审指出的三处「有方案但零测试守护」：
//   1) 逐行分类的等式不变量（评审阻断项：v1.2 的 T15 因缺 otherOk 桶永久算不平）
//   2) P0.1 逐调用点字段归属隔离（评审：ss 调用点若误累加 payload* 会污染归因且无网可查）
//   3) P0.4 明细限流计数器必须是栈上局部（评审：static 跨订阅共享额度且无网可查）
// ============================================================

namespace {

// Base64Utils 只导出 base64Decode()/looksLikeBase64()，没有编码器，且
// src/utils/Base64Utils.cpp 不在 test_subscription_parser 的源列表里，所以这里
// 在测试 TU 内自带 static 编码器（不新增依赖、不改 CMake）。
std::string b64s(const std::string& in) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    char quad[4];
    int n = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned int v = (static_cast<unsigned char>(in[i]) << 16) |
                               (static_cast<unsigned char>(in[i + 1]) << 8) |
                               static_cast<unsigned char>(in[i + 2]);
        quad[0] = tbl[(v >> 18) & 0x3F];
        quad[1] = tbl[(v >> 12) & 0x3F];
        quad[2] = tbl[(v >> 6) & 0x3F];
        quad[3] = tbl[v & 0x3F];
        out.append(quad, 4);
        n = 4;
    }
    if (i < in.size()) {
        const size_t rem = in.size() - i;
        unsigned int v = static_cast<unsigned char>(in[i]) << 16;
        if (rem == 2) v |= static_cast<unsigned char>(in[i + 1]) << 8;
        quad[0] = tbl[(v >> 18) & 0x3F];
        quad[1] = tbl[(v >> 12) & 0x3F];
        quad[2] = (rem == 2) ? tbl[(v >> 6) & 0x3F] : '=';
        quad[3] = '=';
        out.append(quad, 4);
        n = 4;
    }
    (void)n;
    return out;
}

std::string vmessLine(const std::string& json) { return "vmess://" + b64s(json); }

// 捕获 Logger 回调。src/LoggerInstance.cpp:74-79 的 callback 分支位于
// consoleEnabled_ 与 fileEnabled_ 判断之外，对任何级别无条件触发。
class LogCapture {
public:
    LogCapture() {
        Logger::setConsoleEnabled(false);   // 默认 consoleLevel_=INFO 会把 WARN 打到 stdout
        Logger::pushCallback([this](const std::string& msg, LogLevel) { lines_.push_back(msg); });
    }
    ~LogCapture() {
        Logger::popCallback();
        Logger::setConsoleEnabled(true);
    }
    bool contains(const std::string& needle) const {
        for (const std::string& l : lines_) if (l.find(needle) != std::string::npos) return true;
        return false;
    }
    int countPrefix(const std::string& needle) const {
        int n = 0;
        for (const std::string& l : lines_) if (l.find(needle) != std::string::npos) ++n;
        return n;
    }
    size_t size() const { return lines_.size(); }
private:
    std::vector<std::string> lines_;
};

}  // namespace

// --- 守护 1：等式不变量。每一行恰好落入一个桶。 ---
TEST(SubscriptionParserTest, StatsLineBucketsAreExhaustive) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // 2 个正常 vless + 1 个正常 vmess + 1 个畸形 vless(无 @) + 1 行乱码 + 1 空行
    const std::string content =
        "vless://uuid-aaa@h.example.com:443?type=ws#ok1\n"
        "vless://uuid-bbb@h.example.com:443?type=ws#ok2\n"
        + vmessLine("{\"v\":\"2\",\"ps\":\"m1\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}") + "\n"
        "vless://no-at-sign-here\n"
        "just some garbage line\n"
        "\n";

    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-eq", stats);

    EXPECT_EQ(profiles.size(), 3);
    EXPECT_EQ(stats.lineCount, 6);
    EXPECT_EQ(stats.otherOk, 2);
    EXPECT_EQ(stats.vmessOk, 1);
    EXPECT_EQ(stats.droppedMalformedLine, 1);
    EXPECT_EQ(stats.droppedByProtocol[0], 1);
    EXPECT_EQ(stats.unrecognizedLines, 1);
    EXPECT_EQ(stats.droppedEmptyLine, 1);

    // 关键不变量：桶和 == 总行数（独立评审判定 v1.2 此等式因缺 otherOk 而永久算不平）
    EXPECT_EQ(stats.lineCount,
              stats.droppedEmptyLine + stats.metadataCommentLines +
              stats.vmessOk + stats.vmessFailParse +
              stats.vmessSkipEmpty + stats.unrecognizedLines + stats.droppedMalformedLine +
              stats.otherOk);
}

// --- 守护 1b：vmess 失败与载荷过短也进等式 ---
TEST(SubscriptionParserTest, StatsVmessFailureAndSkipEmptyAreCounted) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // 1 个正常 vmess + 1 个载荷解不出 JSON 的 vmess + 1 个载荷过短的 vmess
    const std::string content =
        vmessLine("{\"v\":\"2\",\"ps\":\"m1\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}") + "\n"
        "vmess://QUJD\n"       // "ABC" -> 解出无花括号 -> not JSON
        "vmess://A\n";         // 仅 1 个字母表字符 -> 过短

    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-vm", stats);

    EXPECT_EQ(profiles.size(), 1);
    EXPECT_EQ(stats.vmessOk, 1);
    EXPECT_EQ(stats.vmessSkipEmpty, 1);
    EXPECT_GE(stats.vmessFailParse, 1);
    EXPECT_EQ(stats.lineCount,
              stats.droppedEmptyLine + stats.metadataCommentLines +
              stats.vmessOk + stats.vmessFailParse +
              stats.vmessSkipEmpty + stats.unrecognizedLines + stats.droppedMalformedLine +
              stats.otherOk);
}

// --- 守护 2：逐调用点字段归属隔离。ss 调用点不得污染 payload*。 ---
TEST(SubscriptionParserTest, StatsSsUserInfoDoesNotPollutePayloadCounters) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // userInfo 里的 '-' 属于 ss 级特征，绝不能计入 payloadUrlCharTotal
    const std::string content = "ss://YWVzLTI1Ni1nY206cGFzcy13b3Jk@h.example.com:8388#n\n";
    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-ss", stats);

    // b64("aes-256-gcm:pass-word") 中是否含 '-'/'_' 取决于内容；本例标准 base64
    // 可能刚好不含，所以断言的是「payload* 恒为 0」这条不变量本身。
    EXPECT_EQ(stats.payloadUrlCharLines, 0);
    EXPECT_EQ(stats.payloadUrlCharTotal, 0);
    EXPECT_EQ(stats.payloadNonBase64Chars, 0);
    EXPECT_EQ(stats.payloadPlainJsonLines, 0);
    EXPECT_EQ(stats.payloadUrlEncoded, 0);
    // ss 级字段独立存在
    EXPECT_GE(stats.ssUrlCharCount, 0);
    EXPECT_EQ(stats.otherOk + stats.droppedMalformedLine, 1);  // 该行恰好落入一个桶
}

// --- 守护 3：明细限流计数器是栈上局部，第二次 parse() 仍有自己的额度 ---
TEST(SubscriptionParserTest, StatsFailureDetailBudgetIsPerParseCall) {
    update::SubscriptionParser parser;
    update::ParseStats s1;
    update::ParseStats s2;

    // 5 个全坏的 vmess 行 -> 失败明细应被限流到 3 条
    const std::string bad =
        "vmess://QUJD\n" "vmess://REVG\n" "vmess://R0hJ\n"
        "vmess://TE1P\n" "vmess://TlBQ\n";

    {
        LogCapture cap;
        parser.parse(bad, "s-lim-1", s1);
        EXPECT_EQ(cap.countPrefix("vmess parse failed [subid=s-lim-1"), 3);
        EXPECT_TRUE(cap.contains("vmess summary [subid=s-lim-1]"));
    }
    {
        // 独立额度：若计数器是 static/成员变量，第二次 parse() 将输出 0 条明细
        LogCapture cap;
        parser.parse(bad, "s-lim-2", s2);
        EXPECT_EQ(cap.countPrefix("vmess parse failed [subid=s-lim-2"), 3);
    }
    EXPECT_EQ(s1.vmessFailParse, 5);
    EXPECT_EQ(s2.vmessFailParse, 5);
}

// --- 守护 4：汇总行携带 P4 四个必答问题所需的全部字段 ---
TEST(SubscriptionParserTest, SummaryLogCarriesAttributionFields) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // 构造一个确定含 base64url 特征字符 '-' 的载荷。
    //
    // 踩坑记录（独立评审 B3 的同一类错误，必须写死）：不要指望「让 JSON 自然
    // base64 出 -/_」—— 实测 ASCII JSON 的 base64 输出几乎恒不含 sextet 62/63
    // （ASCII 中仅 '>'(0x3E) / '?'(0x3F) / '~'(0x7E) 三个字符 &0x3F 才等于 62/63，
    // 而 JSON 结构里几乎不出现它们）。v1.1 的 T12 就是这么失效的。
    //
    // P0 的职责是**测量**（统计输入串里有多少 -/_），不是解码，所以这里直接
    // 在合法 base64 载荷上植入一个 '-'，断言计数器把它数出来即可。
    std::string payload = b64s("{\"v\":\"2\",\"ps\":\"m1\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}");
    payload[payload.size() / 2] = '-';
    ASSERT_NE(payload.find('-'), std::string::npos);

    const std::string content =
        vmessLine("{\"v\":\"2\",\"ps\":\"m1\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}") + "\n"
        "vmess://" + payload + "\n";

    LogCapture cap;
    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-sum", stats);
    EXPECT_EQ(profiles.size(), 1);
    EXPECT_EQ(stats.payloadUrlCharLines, 1);
    EXPECT_EQ(stats.payloadUrlCharTotal, 1);

    EXPECT_TRUE(cap.contains("vmess summary [subid=s-sum]"));
    EXPECT_TRUE(cap.contains("payloadUrlLines=1"));
    EXPECT_TRUE(cap.contains("payloadUrlChars="));
    EXPECT_TRUE(cap.contains("payloadPlainJson=0"));
    EXPECT_TRUE(cap.contains("payloadUrlEncoded=0"));
    EXPECT_TRUE(cap.contains("bodyUrlChars="));
    EXPECT_TRUE(cap.contains("ssUrlChars="));
    EXPECT_TRUE(cap.contains("ok=1"));
    EXPECT_TRUE(cap.contains("otherOk=0"));
}

// --- 守护 5：零结果告警对 vless-only 订阅不误报（评审阻断项） ---
TEST(SubscriptionParserTest, ZeroNodeWarningNotFiredForVlessOnlySubscription) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // 2 个正常 vless + 8 个正常 vmess，确保走到「丢弃率」判定而不是「零结果」
    std::string content = "vless://uuid-aaa@h.example.com:443?type=ws#ok1\n"
                          "vless://uuid-bbb@h.example.com:443?type=ws#ok2\n";
    for (int i = 0; i < 8; ++i) {
        content += vmessLine("{\"v\":\"2\",\"ps\":\"m" + std::to_string(i) +
                             "\",\"add\":\"1.2.3." + std::to_string(i + 1) +
                             "\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}") + "\n";
    }

    LogCapture cap;
    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-vless", stats);

    EXPECT_EQ(profiles.size(), 10);
    EXPECT_EQ(stats.otherOk, 2);
    EXPECT_EQ(stats.vmessOk, 8);
    // 关键：vless-only 订阅不得被判为「解析出 0 个节点」
    EXPECT_FALSE(cap.contains("s-vless: parsed 0 nodes"));
}

// --- 守护 6：真正的零结果订阅必须告警 ---
TEST(SubscriptionParserTest, ZeroNodeWarningFiredWhenNothingParsed) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    const std::string content = "vmess://QUJD\n" "garbage line\n" "vless://no-at\n";

    LogCapture cap;
    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-zero", stats);

    EXPECT_EQ(profiles.size(), 0);
    EXPECT_TRUE(cap.contains("s-zero: parsed 0 nodes"));
}

// --- 守护 7：ss:// userInfo 的 %3D 必须先解转义再进 base64 ---
//
// 真实数据（crackbest 订阅，74 条 ss:// 中 14 条命中）：面板在生成分享链接时对
// userInfo 做了 URL 编码，base64 的 padding '=' 被写成 '%3D'。
//
// 修复前的真实行为**不是「节点被丢弃」，而是「密码被静默改错」**（这一点必须在
// 测试注释里写死，否则后人会按错误的假设去改断言）：
//   decodeBase64 丢掉 '%'、把 '3D' 当成两个有效数据字符，
//   于是 32 字符的正确 base64 变成 33 字符 -> 解出 24 字节
//   "aes-128-gcm:shadowsocks7"（末尾多出一个字符）。
//   该串仍然是可打印 ASCII，能通过 isPrintableAscii 检查，于是节点照常入库，
//   但密码是错的 —— 连接必然失败，且没有任何日志提示。
//
// 反向注意：本用例**绝不能**改用 SubscriptionParser::urlDecode() 去做那步解码。
// urlDecode 第二轮会把每个字面 '+' 替换成空格，而 '+' 是 base64 的 sextet 62，
// 那样会把本来能解的节点再次破坏掉。
TEST(SubscriptionParserTest, SsUserInfoPercentEncodedPaddingIsDecodedBeforeBase64) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    // b64("aes-128-gcm:shadowsocks") + padding '=' 被编码成 %3D
    //   正确形态： YWVzLTEyOC1nY206c2hhZG93c29ja3M=
    //   线上形态： YWVzLTEyOC1nY206c2hhZG93c29ja3M%3D
    const std::string content =
        "ss://YWVzLTEyOC1nY206c2hhZG93c29ja3M%3D@158.173.20.208:443#ss-pct\n";

    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-sspct", stats);

    ASSERT_EQ(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "3");
    EXPECT_EQ(profiles[0].address, "158.173.20.208");
    EXPECT_EQ(profiles[0].port, "443");
    EXPECT_EQ(profiles[0].security, "aes-128-gcm");
    // 关键断言：修复前得到的是 "shadowsocks7"（多一个字符），修复后精确还原。
    EXPECT_EQ(profiles[0].id, "shadowsocks");
    EXPECT_EQ(stats.otherOk, 1);
    EXPECT_EQ(stats.droppedMalformedLine, 0);
}

// --- 守护 8：行首 '#' 的订阅元数据注释单列，不计入 unrecognizedLines ---
//
// 真实数据（MatinGhanbari 订阅）：v2rayN / Clash Verge 等面板会在明文订阅里以
// "#profile-title:"、 "#subscription-userinfo:"、 "#support-url:"、
// "#profile-update-interval:" 开头的行给出展示名与流量配额。
// 修复前这些行落进末尾的 else 分支被计入 unrecognizedLines，从而把「丢弃率」
// 统计污染成假告警（v1.2 的 P0.5 第二档正是按丢弃率判定）。
TEST(SubscriptionParserTest, MetadataCommentLinesAreCountedSeparately) {
    update::SubscriptionParser parser;
    update::ParseStats stats;

    const std::string content =
        "#profile-title: My Subscription\n"
        "#subscription-userinfo: upload=1; download=2; total=3\n"
        "#support-url: https://example.com/support\n"
        "vless://uuid-aaa@h.example.com:443?type=ws#ok1\n"
        "vless://uuid-bbb@h.example.com:443?type=ws#ok2\n"
        "vless://uuid-ccc@h.example.com:443?type=ws#ok3\n"
        "vless://uuid-ddd@h.example.com:443?type=ws#ok4\n"
        "unknown-protocol://whatever\n";

    LogCapture cap;
    std::vector<db::models::Profileitem> profiles = parser.parse(content, "s-meta", stats);

    EXPECT_EQ(profiles.size(), 4);
    EXPECT_EQ(stats.metadataCommentLines, 3);
    // 元数据注释不得混进 unrecognizedLines，否则丢弃率告警失真
    EXPECT_EQ(stats.unrecognizedLines, 1);  // 只剩那 1 行真正无法识别的协议
    EXPECT_EQ(stats.otherOk, 4);
    EXPECT_EQ(stats.lineCount, 8);
    EXPECT_EQ(stats.lineCount,
              stats.droppedEmptyLine + stats.metadataCommentLines +
              stats.vmessOk + stats.vmessFailParse +
              stats.vmessSkipEmpty + stats.unrecognizedLines + stats.droppedMalformedLine +
              stats.otherOk);
    // summary 行必须带上 metaComment，否则汇总无法复算
    EXPECT_TRUE(cap.contains("metaComment=3"));
    // 丢弃率判据：(unrecognized + malformed) * 2 > parsedOk。
    //   正确口径：1 * 2 = 2 > 4 为假 -> 不告警
    //   若元数据注释被误计入 unrecognized：4 * 2 = 8 > 4 为真 -> 会误告警
    // 这条断言因此是有鉴别力的。
    EXPECT_FALSE(cap.contains("s-meta: drop rate"));
}