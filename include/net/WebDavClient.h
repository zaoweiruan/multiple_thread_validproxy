// include/net/WebDavClient.h - minimal RFC 4918 WebDAV client (GET/PUT/PROPFIND/MKCOL).
//
// Only the subset needed by cloud sync is implemented. TLS verification stays on;
// redirects are not followed, so credentials can never be replayed to another host.
#ifndef NET_WEBDAV_CLIENT_H
#define NET_WEBDAV_CLIENT_H

#include <string>
#include <vector>

#include "net/WebDavCredentials.h"

namespace net {

struct DavEntry {
    std::string href;           // RFC 4918 <D:href>
    long long contentLength = -1;
    std::string lastModified;   // <D:getlastmodified>, kept verbatim (HTTP-date)
};

struct DavResponse {
    long status = 0;      // HTTP status code (0 when the transfer failed)
    std::string body;     // GET body / error body
    std::string error;    // curl-level transport error
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class WebDavClient {
public:
    WebDavClient(WebDavCredentials credentials, std::string baseUrl);

    DavResponse get(const std::string& remotePath) const;
    DavResponse put(const std::string& remotePath, const std::string& body,
                    const std::string& ifUnmodifiedSince = "") const;
    DavResponse propfind(const std::string& remotePath, int depth = 0) const;
    DavResponse mkcol(const std::string& remotePath) const;

    // ---- pure helpers (unit-tested, no network) ----
    static std::string buildUrl(const std::string& baseUrl, const std::string& remotePath);
    static std::vector<DavEntry> parseMultiStatus(const std::string& xml);
    static std::string normalizeHref(const std::string& href);
    // Percent-encodes each path segment, keeps '/'
    static std::string encodePath(const std::string& path);

private:
    DavResponse perform(const std::string& method, const std::string& remotePath,
                        const std::string& uploadBody, const char* depthHeader,
                        const char* ifUnmodifiedSince, bool wantBody) const;

    WebDavCredentials credentials_;
    std::string baseUrl_;
};

} // namespace net

#endif // NET_WEBDAV_CLIENT_H
