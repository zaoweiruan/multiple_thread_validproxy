// src/net/WebDavCredentials.cpp
#include "net/WebDavCredentials.h"
#include "i18n/I18n.h"

#include <cctype>
#include <cstddef>

namespace net {

namespace {

// Obfuscation key. Deliberately arbitrary: it does not protect anything on its own
// (it ships in the same binary), it only keeps the plaintext from appearing in
// `strings` output and in the repository.
const char* const kBlobKey = "validproxy-webdav-v1";

const char kBase64Alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string xorApply(const std::string& input, const std::string& key) {
    if (key.empty()) {
        return input;
    }
    std::string out = input;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(out[i] ^ key[i % key.size()]);
    }
    return out;
}

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

} // namespace

std::string WebDavCredentialsStore::base64Encode(const std::string& raw) {
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < raw.size()) {
        const unsigned int triple =
                (static_cast<unsigned char>(raw[i]) << 16) |
                (static_cast<unsigned char>(raw[i + 1]) << 8) |
                static_cast<unsigned char>(raw[i + 2]);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[triple & 0x3F]);
        i += 3;
    }
    const std::size_t rest = raw.size() - i;
    if (rest == 1) {
        const unsigned int triple = static_cast<unsigned char>(raw[i]) << 16;
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rest == 2) {
        const unsigned int triple = (static_cast<unsigned char>(raw[i]) << 16) |
                                    (static_cast<unsigned char>(raw[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool WebDavCredentialsStore::base64Decode(const std::string& encoded, std::string& raw) {
    raw.clear();
    int values[4] = {0, 0, 0, 0};
    int count = 0;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c == '=' || std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (c == '=') {
                break;
            }
            continue;
        }
        const char* pos = nullptr;
        for (int k = 0; k < 64; ++k) {
            if (kBase64Alphabet[k] == c) {
                pos = kBase64Alphabet + k;
                break;
            }
        }
        if (pos == nullptr) {
            return false;
        }
        values[count++] = static_cast<int>(pos - kBase64Alphabet);
        if (count == 4) {
            const unsigned int triple = (static_cast<unsigned int>(values[0]) << 18) |
                                        (static_cast<unsigned int>(values[1]) << 12) |
                                        (static_cast<unsigned int>(values[2]) << 6) |
                                        static_cast<unsigned int>(values[3]);
            raw.push_back(static_cast<char>((triple >> 16) & 0xFF));
            raw.push_back(static_cast<char>((triple >> 8) & 0xFF));
            raw.push_back(static_cast<char>(triple & 0xFF));
            count = 0;
        }
    }
    if (count == 2) {
        const unsigned int triple = (static_cast<unsigned int>(values[0]) << 18) |
                                    (static_cast<unsigned int>(values[1]) << 12);
        raw.push_back(static_cast<char>((triple >> 16) & 0xFF));
    } else if (count == 3) {
        const unsigned int triple = (static_cast<unsigned int>(values[0]) << 18) |
                                    (static_cast<unsigned int>(values[1]) << 12) |
                                    (static_cast<unsigned int>(values[2]) << 6);
        raw.push_back(static_cast<char>((triple >> 16) & 0xFF));
        raw.push_back(static_cast<char>((triple >> 8) & 0xFF));
    } else if (count != 0) {
        return false;
    }
    return true;
}

std::string WebDavCredentialsStore::encodeBlob(const std::string& user,
                                               const std::string& password,
                                               const std::string& key) {
    const std::string plain = user + "\n" + password;
    return base64Encode(xorApply(plain, key));
}

bool WebDavCredentialsStore::decodeBlob(const std::string& blob, const std::string& key,
                                        std::string& user, std::string& password) {
    user.clear();
    password.clear();
    std::string raw;
    if (!base64Decode(trim(blob), raw)) {
        return false;
    }
    const std::string plain = xorApply(raw, key);
    const std::size_t sep = plain.find('\n');
    if (sep == std::string::npos) {
        return false;
    }
    user = plain.substr(0, sep);
    password = plain.substr(sep + 1);
    return !user.empty();
}

// Defined in the generated translation unit. Empty string when not provisioned.
extern const char* const kWebDavSecretBlob;

bool WebDavCredentialsStore::load(WebDavCredentials& out, std::string& error) {
    out = WebDavCredentials();
    error.clear();

    const std::string blob = kWebDavSecretBlob != nullptr ? kWebDavSecretBlob : "";
    if (trim(blob).empty()) {
        error = i18n::u8(i18n::StrId::WebDavCredentials_WebDAVCredentialsConfiguredRun);
        return false;
    }
    if (!decodeBlob(blob, kBlobKey, out.user, out.password)) {
        error = i18n::u8(i18n::StrId::WebDavCredentials_WebDAVCredentialsCannotParsed);
        return false;
    }
    if (!out.usable()) {
        error = i18n::u8(i18n::StrId::WebDavCredentials_WebDAVCredentialsIncompleteAccount);
        return false;
    }
    return true;
}

} // namespace net
