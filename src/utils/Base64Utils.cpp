#include "utils/Base64Utils.h"
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace utils {

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<uint8_t> base64Decode(const std::string& input) {
    std::vector<uint8_t> decoded;
    if (input.empty()) {
        return decoded;
    }

    // Basic validation: length must be multiple of 4
    if (input.length() % 4 != 0) {
        return decoded;
    }

    // Check if all characters are valid Base64
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c != '=' && base64_chars.find(c) == std::string::npos) {
            return decoded;
        }
    }

    // Decode
    unsigned int pos = 0;
    std::vector<unsigned char> buffer(4);
    std::vector<unsigned int> index(256, 0);

    for (unsigned int i = 0; i < 64; ++i) {
        index[static_cast<unsigned char>(base64_chars[i])] = i;
    }
    index[64] = 0; // '='
    index[65] = 0; // '='

    while (pos < input.length()) {
        unsigned int len = 0;
        for (unsigned int i = 0; i < 4; ++i) {
            char c = input[pos + i];
            if (c == '=') {
                buffer[i] = 0;
                if (i == 0) { len = 0; break; }
                if (i == 1) { len = 1; break; }
                if (i == 2) { len = 2; break; }
            } else {
                buffer[i] = static_cast<unsigned char>(index[static_cast<unsigned char>(c)]);
            }
            ++len;
        }
        pos += 4;

        decoded.push_back(static_cast<uint8_t>((buffer[0] << 2) | (buffer[1] >> 4)));
        if (len > 1) {
            decoded.push_back(static_cast<uint8_t>((buffer[1] << 4) | (buffer[2] >> 2)));
        }
        if (len > 2) {
            decoded.push_back(static_cast<uint8_t>((buffer[2] << 6) | buffer[3]));
        }
    }

    return decoded;
}

bool looksLikeBase64(const std::string& input) {
    if (input.empty() || input.length() % 4 != 0) {
        return false;
    }
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c != '=' && base64_chars.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace utils
