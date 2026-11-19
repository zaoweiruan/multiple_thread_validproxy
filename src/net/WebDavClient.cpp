// src/net/WebDavClient.cpp
#include "net/WebDavClient.h"
#include "i18n/I18n.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "CurlEasyHandle.h"
#include "Logger.h"

namespace net {

namespace {

std::once_flag g_curlInit;

void ensureCurlGlobal() {
    std::call_once(g_curlInit, []() { curl_global_init(CURL_GLOBAL_ALL); });
}

std::size_t writeToString(char* data, std::size_t size, std::size_t nmemb, void* userp) {
    std::string* out = static_cast<std::string*>(userp);
    const std::size_t total = size * nmemb;
    out->append(data, total);
    return total;
}

struct ReadCtx {
    const std::string* body = nullptr;
    std::size_t offset = 0;
};

std::size_t readFromString(char* buffer, std::size_t size, std::size_t nitems, void* userp) {
    ReadCtx* ctx = static_cast<ReadCtx*>(userp);
    const std::size_t want = size * nitems;
    if (ctx == nullptr || ctx->body == nullptr) {
        return 0;
    }
    const std::size_t left = ctx->body->size() - ctx->offset;
    const std::size_t take = (want < left) ? want : left;
    if (take > 0) {
        std::memcpy(buffer, ctx->body->data() + ctx->offset, take);
        ctx->offset += take;
    }
    return take;
}

std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

// Extracts the text of the first <tag ...> ... </tag> inside `scope`.
bool extractFirst(const std::string& scope, const std::string& tag, std::string& out) {
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    std::size_t start = scope.find(open);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t headEnd = scope.find('>', start);
    if (headEnd == std::string::npos) {
        return false;
    }
    const std::size_t bodyStart = headEnd + 1;
    const std::size_t end = scope.find(close, bodyStart);
    if (end == std::string::npos) {
        return false;
    }
    out = scope.substr(bodyStart, end - bodyStart);
    return true;
}

} // namespace

WebDavClient::WebDavClient(WebDavCredentials credentials, std::string baseUrl)
    : credentials_(credentials), baseUrl_(baseUrl) {
    if (!baseUrl_.empty() && baseUrl_.back() != '/') {
        baseUrl_.push_back('/');
    }
    ensureCurlGlobal();
}

std::string WebDavClient::encodePath(const std::string& path) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        const bool unreserved = (std::isalnum(c) != 0) || c == '-' || c == '_' || c == '.' ||
                                c == '~' || c == '/';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string WebDavClient::buildUrl(const std::string& baseUrl,
                                   const std::string& remotePath) {
    std::string base = baseUrl;
    if (!base.empty() && base.back() != '/') {
        base.push_back('/');
    }
    std::string rel = remotePath;
    while (!rel.empty() && rel.front() == '/') {
        rel.erase(rel.begin());
    }
    return base + encodePath(rel);
}

std::string WebDavClient::normalizeHref(const std::string& href) {
    std::string out = href;
    // Strip scheme://host
    const std::size_t scheme = out.find("://");
    if (scheme != std::string::npos) {
        const std::size_t slash = out.find('/', scheme + 3);
        out = (slash == std::string::npos) ? std::string("/") : out.substr(slash);
    }
    // Percent-decode
    std::string decoded;
    decoded.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '%' && i + 2 < out.size()) {
            const char hi = out[i + 1];
            const char lo = out[i + 2];
            const int hiVal = (hi >= '0' && hi <= '9') ? hi - '0'
                              : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                              : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
            const int loVal = (lo >= '0' && lo <= '9') ? lo - '0'
                              : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                              : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
            if (hiVal >= 0 && loVal >= 0) {
                decoded.push_back(static_cast<char>((hiVal << 4) | loVal));
                i += 2;
                continue;
            }
        }
        decoded.push_back(out[i]);
    }
    return decoded;
}

std::vector<DavEntry> WebDavClient::parseMultiStatus(const std::string& xml) {
    std::vector<DavEntry> entries;
    const std::string lower = lowerAscii(xml);
    std::size_t pos = 0;
    while (true) {
        const std::size_t begin = lower.find("<d:response", pos);
        if (begin == std::string::npos) {
            break;
        }
        const std::size_t end = lower.find("</d:response>", begin);
        if (end == std::string::npos) {
            break;  // truncated document: stop, keep what we have
        }
        const std::string block = xml.substr(begin, end - begin);
        pos = end + 1;

        DavEntry entry;
        std::string href;
        if (extractFirst(block, "D:href", href) || extractFirst(block, "d:href", href)) {
            entry.href = normalizeHref(href);
        }
        std::string lengthText;
        if (extractFirst(block, "D:getcontentlength", lengthText) ||
            extractFirst(block, "d:getcontentlength", lengthText)) {
            entry.contentLength = std::strtoll(lengthText.c_str(), nullptr, 10);
        }
        std::string mtime;
        if (extractFirst(block, "D:getlastmodified", mtime) ||
            extractFirst(block, "d:getlastmodified", mtime)) {
            entry.lastModified = mtime;
        }
        if (!entry.href.empty()) {
            entries.push_back(entry);
        }
    }
    return entries;
}

DavResponse WebDavClient::perform(const std::string& method, const std::string& remotePath,
                                  const std::string& uploadBody, const char* depthHeader,
                                  const char* ifUnmodifiedSince, bool wantBody) const {
    DavResponse response;
    CurlEasyHandle handle;
    if (!handle.valid()) {
        response.error = i18n::u8(i18n::StrId::WebDavClient_CurlInitialisationFailed);
        return response;
    }

    const std::string url = buildUrl(baseUrl_, remotePath);
    handle.setUrl(url).setFollowLocation(false).setSslVerifyPeer(true).setSslVerifyHost(true);
    handle.setConnectTimeoutMs(5000).setTimeoutMs(10000);

    CURL* curl = handle.get();
    curl_easy_setopt(curl, CURLOPT_USERNAME, credentials_.user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, credentials_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "validproxy-webdav/1");

    if (wantBody) {
        handle.setWriteCallback(writeToString, &response.body);
    } else {
        handle.setNoBody(true);
    }

    struct curl_slist* headers = nullptr;
    if (std::strcmp(method.c_str(), "GET") != 0) {
        headers = curl_slist_append(headers, ("X-HTTP-Method-Override: " + method).c_str());
        headers = curl_slist_append(headers, "X-Expected-Entity-Length: 0");
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    if (depthHeader != nullptr) {
        headers = curl_slist_append(headers, depthHeader);
    }
    if (ifUnmodifiedSince != nullptr && ifUnmodifiedSince[0] != '\0') {
        headers = curl_slist_append(headers, ifUnmodifiedSince);
    }

    ReadCtx readCtx;
    if (method == "PUT") {
        readCtx.body = &uploadBody;
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromString);
        curl_easy_setopt(curl, CURLOPT_READDATA, &readCtx);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                         static_cast<curl_off_t>(uploadBody.size()));
    }
    if (headers != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode code = curl_easy_perform(curl);
    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }

    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
    }
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = status;
    return response;
}

DavResponse WebDavClient::get(const std::string& remotePath) const {
    return perform("GET", remotePath, std::string(), nullptr, nullptr, true);
}

DavResponse WebDavClient::put(const std::string& remotePath, const std::string& body,
                              const std::string& ifUnmodifiedSince) const {
    return perform("PUT", remotePath, body, nullptr, ifUnmodifiedSince.c_str(), false);
}

DavResponse WebDavClient::propfind(const std::string& remotePath, int depth) const {
    const std::string depthHeader = "Depth: " + std::to_string(depth);
    return perform("PROPFIND", remotePath, std::string(), depthHeader.c_str(), nullptr, true);
}

DavResponse WebDavClient::mkcol(const std::string& remotePath) const {
    return perform("MKCOL", remotePath, std::string(), nullptr, nullptr, false);
}

} // namespace net
