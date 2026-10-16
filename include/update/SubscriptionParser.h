#ifndef SUBSCRIPTION_PARSER_H
#define SUBSCRIPTION_PARSER_H

#include <string>
#include <vector>
#include "Profileitem.h"

namespace update {

// P0 可观测性：逐行分类统计。口径为「进入 while(getline) 的每一行恰好落入
// lineCount == droppedEmptyLine + vmessOk + vmessFailParse + vmessSkipEmpty
//                    + unrecognizedLines + droppedMalformedLine + otherOk
//                    + metadataCommentLines
// 之一」，用于回答 P4 的归因问题。只含 bool/long，无 Boost 依赖。
struct ParseStats {
    // ---- 行级分类 ----
    long lineCount            = 0;  // 总行数（含随后被丢弃的空行）
    long droppedEmptyLine     = 0;  // 空行
    long vmessOk              = 0;  // vmess 行成功入 profiles
    long vmessFailParse       = 0;  // vmess 行解析失败（含经裁剪救回的）
    long vmessSkipEmpty       = 0;  // vmess 载荷有效字母表字符 < 2
    long unrecognizedLines    = 0;  // 不属于任何已知 scheme
    long droppedMalformedLine = 0;  // 协议级畸形丢弃（vless/ss/trojan/hy2）
    long otherOk              = 0;  // 非 vmess 行成功入 profiles
    // 行首 '#' 的订阅元数据注释（#profile-title: / #subscription-userinfo: 等）。
    // 单列而不计入 unrecognizedLines：它们本来就不是节点，计进去会让丢弃率告警失真。
    long metadataCommentLines = 0;
    long droppedByProtocol[6] = {0, 0, 0, 0, 0, 0};  // 0=vless 1=ss 2=trojan 3=hy2

    // vmessFailSalvaged 是 vmessFailParse 的子集，**不进上面的等式**（否则重复计数），
    // 单列供日志与 P4 判断「裁剪重试的救回率」。
    long vmessFailSalvaged    = 0;

    // 以下三个均为**纯观察字段，不进上面的等式**（改写/恢复不改变行的分类归属）。
    // vmessMislabeledPlainVless: 误挂 "vmess://" 前缀的明文 vless 行（改写后按 vless 解析）。
    // 该行最终落 otherOk 桶（countedAsVmess 保持 false），等式不受影响。
    long vmessMislabeledPlainVless = 0;
    // ssSip001Recovered: 旧式 SIP001（整段 base64 无 '@'）经容错恢复的 ss 行。
    // 该行最终落 otherOk 桶，等式不受影响。
    long ssSip001Recovered    = 0;

    // ---- body 级（仅整段订阅 :51 调用点写入） ----
    bool bodyHadProtocol      = false;
    bool bodyDecodeApplied    = false;
    bool bodyDecodeFellBack   = false;
    long bodyUrlCharCount     = 0;  // body 中 '-' 与 '_' 总数（base64url 特征）
    long bodyNonBase64Chars   = 0;  // body 中被 decodeBase64 丢弃的字符数
    long bodyDecodedLength    = 0;

    // ---- payload 级（仅 vmess 载荷 :77 调用点写入） ----
    long payloadUrlCharTotal   = 0;
    long payloadUrlCharLines   = 0;  // 含至少一个 '-' 或 '_' 的载荷行数
    long payloadNonBase64Chars = 0;
    long payloadPlainJsonLines = 0;  // 由 P3.1 写入；P0 阶段恒 0
    long payloadUrlEncoded     = 0;  // 由 P3.2 写入；P0 阶段恒 0

    // ---- ss 级（仅 ss userInfo :364 调用点写入） ----
    long ssUrlCharCount       = 0;
};

class SubscriptionParser {
public:
    SubscriptionParser() = default;

    std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid);
    // P0 可观测性重载：原签名保留并委托到此版本。
    std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid,
                                                ParseStats& stats);

private:
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);
    std::pair<std::string, std::string> parseAddressPort(const std::string& addrPart);
};

} // namespace update

#endif // SUBSCRIPTION_PARSER_H