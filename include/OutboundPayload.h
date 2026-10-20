#pragma once

#include <cstddef>
#include <string>

#include <boost/system/error_code.hpp>
#include <boost/json.hpp>

namespace outbound_payload {

// 与 XrayApi::parseOutboundJson 的 outbounds[0] 语义严格对齐（src/XrayApi.cpp:716-717）。
// CLI 回退路径（xray api ado）会把载荷里的每个 outbound 依次导入，而测试实例
// 自带 direct，因此多 outbound 文档必然报 `existing tag found: direct`。
inline bool normalizeForAdo(const std::string& payload, std::string& normalized) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(payload, ec);
    if (ec || !parsed.is_object()) {
        return false;
    }

    boost::json::object const& root = parsed.get_object();
    boost::json::value const* outbounds = root.if_contains("outbounds");
    if (outbounds == nullptr || !outbounds->is_array()) {
        return false;
    }

    boost::json::array const& list = outbounds->get_array();
    if (list.empty() || !list.front().is_object()) {
        return false;
    }

    boost::json::array single;
    single.push_back(list.front());

    boost::json::object out;
    out["outbounds"] = single;
    normalized = boost::json::serialize(out);
    return true;
}

inline std::size_t outboundCount(const std::string& payload) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(payload, ec);
    if (ec || !parsed.is_object()) {
        return 0;
    }

    boost::json::object const& root = parsed.get_object();
    boost::json::value const* outbounds = root.if_contains("outbounds");
    if (outbounds == nullptr || !outbounds->is_array()) {
        return 0;
    }

    return outbounds->get_array().size();
}

} // namespace outbound_payload
