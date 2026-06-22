#include "share/VMessUriBuilder.h"
#include "share/UriCodec.h"
#include <sstream>

namespace share {

std::string VMessUriBuilder::build(const db::models::Profileitem& profile) {
    std::string json;

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"v\": \"" << UriCodec::jsonEncode("2") << "\",\n";
    oss << "  \"ps\": \"" << UriCodec::jsonEncode(profile.remarks) << "\",\n";
    oss << "  \"add\": \"" << UriCodec::jsonEncode(profile.address) << "\",\n";
    oss << "  \"port\": \"" << UriCodec::jsonEncode(profile.port) << "\",\n";
    oss << "  \"id\": \"" << UriCodec::jsonEncode(profile.id) << "\",\n";
    std::string alterId = profile.alterid.empty() ? "0" : profile.alterid;
    oss << "  \"aid\": \"" << UriCodec::jsonEncode(alterId) << "\",\n";
    oss << "  \"scy\": \"" << UriCodec::jsonEncode(profile.security) << "\",\n";
    oss << "  \"net\": \"" << UriCodec::jsonEncode(profile.network) << "\",\n";
    oss << "  \"type\": \"" << UriCodec::jsonEncode(profile.headertype) << "\",\n";
    oss << "  \"host\": \"" << UriCodec::jsonEncode(profile.requesthost) << "\",\n";
    oss << "  \"path\": \"" << UriCodec::jsonEncode(profile.path) << "\",\n";
    oss << "  \"tls\": \"" << UriCodec::jsonEncode(profile.streamsecurity) << "\",\n";
    oss << "  \"sni\": \"" << UriCodec::jsonEncode(profile.sni) << "\",\n";
    oss << "  \"alpn\": \"" << UriCodec::jsonEncode(profile.alpn) << "\",\n";
    oss << "  \"fp\": \"" << UriCodec::jsonEncode(profile.fingerprint) << "\",\n";
    std::string insecureVal = profile.allowinsecure.empty() ? "0" : profile.allowinsecure;
    oss << "  \"insecure\": \"" << UriCodec::jsonEncode(insecureVal) << "\"\n";
    oss << "}";
    json = oss.str();

    std::string b64 = UriCodec::base64Encode(json);
    return "vmess://" + b64;
}

} // namespace share
