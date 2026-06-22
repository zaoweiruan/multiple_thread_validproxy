#ifndef URI_CODEC_H
#define URI_CODEC_H

#include <string>
#include <map>

namespace share {

class UriCodec {
public:
    static std::string base64Encode(const std::string& input);
    static std::string base64Decode(const std::string& input);
    static std::string urlEncodeStandard(const std::string& input);
    static std::string urlEncodeRemarksOrPath(const std::string& input);
    static std::string jsonEncode(const std::string& input);
    static std::string buildQueryString(const std::map<std::string, std::string>& params);

private:
    static void replaceAll(std::string& str, const std::string& from, const std::string& to);
    static const char* base64_chars;
};

} // namespace share

#endif // URI_CODEC_H
