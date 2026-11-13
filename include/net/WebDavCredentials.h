// include/net/WebDavCredentials.h - embedded WebDAV account/app-password.
//
// SECURITY NOTE (deliberate, documented in
// docs/specs/2026-11-13-Spec-WebDavCloudSync-v1.0.md §5):
// the account and app-password are shipped inside the binary, obfuscated with a
// fixed key. That is obfuscation, NOT confidentiality - anyone holding the exe
// can recover them. It exists so credentials never sit in config.json, the repo,
// or the logs. Real secrecy would require DPAPI / the Windows credential store.
#ifndef NET_WEBDAV_CREDENTIALS_H
#define NET_WEBDAV_CREDENTIALS_H

#include <string>

namespace net {

struct WebDavCredentials {
    std::string user;
    std::string password;
    bool usable() const { return !user.empty() && !password.empty(); }
};

// Controls where the secrets live in the remote tree. Kept in code (not config)
// because it is part of the account setup rather than user preference.
struct WebDavEndpoint {
    std::string baseUrl;    // e.g. https://dav.jianguoyun.com/dav/
    std::string remoteDir;  // e.g. validproxy
};

class WebDavCredentialsStore {
public:
    // Decodes the embedded blob. Returns false when no blob was provisioned
    // (build without resources/webdav_secret.inc) or it is malformed.
    static bool load(WebDavCredentials& out, std::string& error);

    // Exposed for tests: XOR + Base64, tolerant of surrounding whitespace.
    static std::string encodeBlob(const std::string& user, const std::string& password,
                                  const std::string& key);
    static bool decodeBlob(const std::string& blob, const std::string& key,
                           std::string& user, std::string& password);

    static std::string base64Encode(const std::string& raw);
    static bool base64Decode(const std::string& encoded, std::string& raw);
};

} // namespace net

#endif // NET_WEBDAV_CREDENTIALS_H
