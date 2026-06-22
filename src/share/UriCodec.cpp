#include "share/UriCodec.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdio>

namespace share {

const char* UriCodec::base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void UriCodec::replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string UriCodec::base64Encode(const std::string& input) {
    std::string result;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    int len = input.length();
    int pos = 0;
    
    while (len--) {
        char_array_3[i++] = input[pos++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++) result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = 0;
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        if (i == 1) {
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2);
        } else {
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
        }
        for (j = 0; j < i + 1; j++) result += base64_chars[char_array_4[j]];
        while(i++ < 3) result += '=';
    }
    
    return result;
}

std::string UriCodec::base64Decode(const std::string& input) {
    std::string result;
    int i = 0;
    int j = 0;
    int len = input.length();
    unsigned char char_array_4[4];
    unsigned char char_array_3[3];
    std::string base64_table = base64_chars;
    
    while (len-- && (input[j] != '=') && (std::isalnum(input[j]) || input[j] == '+' || input[j] == '/')) {
        char_array_4[i++] = input[j++];
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = (unsigned char)base64_table.find(char_array_4[i]);
            }
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (i = 0; i < 3; i++) result += char_array_3[i];
            i = 0;
        }
    }
    
    if (i) {
        for (j = 0; j < i; j++) {
            char_array_4[j] = (unsigned char)base64_table.find(char_array_4[j]);
        }
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (j = 0; j < i - 1; j++) result += char_array_3[j];
    }
    
    return result;
}

std::string UriCodec::urlEncodeStandard(const std::string& input) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;
    
    for (char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

std::string UriCodec::buildQueryString(const std::map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;
    for (const std::pair<const std::string, std::string>& entry : params) {
        if (!entry.second.empty()) {
            if (!first) oss << "&";
            oss << urlEncodeStandard(entry.first) << "=" << urlEncodeStandard(entry.second);
            first = false;
        }
    }
    return oss.str();
}

std::string UriCodec::jsonEncode(const std::string& input) {
    std::string result = input;
    replaceAll(result, "\\", "\\\\");
    replaceAll(result, "\"", "\\\"");
    return result;
}

std::string UriCodec::urlEncodeRemarksOrPath(const std::string& input) {
    std::string result;
    for (unsigned char c : input) {
        if (c >= 0x80) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        } else if (c == ';' || c == '=' || c == ' ' || c == '|' || c == '#' || c == '?' || c == '/' || c == '\\') {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        } else {
            result += (char)c;
        }
    }
    return result;
}

} // namespace share
