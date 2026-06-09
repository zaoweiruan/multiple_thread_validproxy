#ifndef CURL_EASY_HANDLE_H
#define CURL_EASY_HANDLE_H

#include <curl/curl.h>
#include <string>
#include <stdexcept>
#include <utility>

class CurlEasyHandle {
public:
    // RAII: Initialize in constructor, throw on failure
    CurlEasyHandle() : curl_(curl_easy_init()) {
        if (!curl_) {
            throw std::runtime_error("curl_easy_init() failed");
        }
    }

    // RAII: Cleanup in destructor
    ~CurlEasyHandle() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    // No copy
    CurlEasyHandle(const CurlEasyHandle&) = delete;
    CurlEasyHandle& operator=(const CurlEasyHandle&) = delete;

    // Move semantics
    CurlEasyHandle(CurlEasyHandle&& other) noexcept : curl_(other.curl_) {
        other.curl_ = nullptr;
    }

    CurlEasyHandle& operator=(CurlEasyHandle&& other) noexcept {
        if (this != &other) {
            if (curl_) {
                curl_easy_cleanup(curl_);
            }
            curl_ = other.curl_;
            other.curl_ = nullptr;
        }
        return *this;
    }

    // Accessor (like Database::get())
    CURL* get() const { return curl_; }

    // Check if handle is valid (not moved-from)
    bool valid() const { return curl_ != nullptr; }

    // Fluent API: set URL
    CurlEasyHandle& setUrl(const std::string& url) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_URL, url.c_str()), "setUrl");
        return *this;
    }

    // Fluent API: set proxy
    CurlEasyHandle& setProxy(const std::string& proxyUrl) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_PROXY, proxyUrl.c_str()), "setProxy");
        return *this;
    }

    // Fluent API: set timeout in milliseconds
    CurlEasyHandle& setTimeoutMs(long ms) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, ms), "setTimeoutMs");
        return *this;
    }

    // Fluent API: set connect timeout in milliseconds
    CurlEasyHandle& setConnectTimeoutMs(long ms) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, ms), "setConnectTimeoutMs");
        return *this;
    }

    // Fluent API: set timeout in seconds
    CurlEasyHandle& setTimeoutSec(long sec) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_TIMEOUT, sec), "setTimeoutSec");
        return *this;
    }

    // Fluent API: set NOBODY (HEAD request)
    CurlEasyHandle& setNoBody(bool enable = true) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_NOBODY, enable ? 1L : 0L), "setNoBody");
        return *this;
    }

    // Fluent API: set follow location
    CurlEasyHandle& setFollowLocation(bool enable = true) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, enable ? 1L : 0L), "setFollowLocation");
        return *this;
    }

    // Fluent API: set SSL verify peer
    CurlEasyHandle& setSslVerifyPeer(bool enable) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, enable ? 1L : 0L), "setSslVerifyPeer");
        return *this;
    }

    // Fluent API: set SSL verify host
    CurlEasyHandle& setSslVerifyHost(bool enable) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, enable ? 1L : 0L), "setSslVerifyHost");
        return *this;
    }

    // Fluent API: set write callback (for GET requests)
    // curl_write_callback = size_t(*)(char*, size_t, size_t, void*)
    CurlEasyHandle& setWriteCallback(curl_write_callback cb, void* userData) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, cb), "setWriteCallback");
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_WRITEDATA, userData), "setWriteCallback(userData)");
        return *this;
    }

    // Static write callback for collecting response data into std::string
    // Signature matches curl_write_callback: size_t(*)(char*, size_t, size_t, void*)
    static size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
        auto* str = static_cast<std::string*>(userp);
        str->append(contents, size * nmemb);
        return size * nmemb;
    }

    // Perform request (throws on failure)
    void perform() {
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_perform failed: ") + curl_easy_strerror(res));
        }
    }

    // Get total time (call after perform())
    double getTotalTime() const {
        double totalTime = 0.0;
        curl_easy_getinfo(curl_, CURLINFO_TOTAL_TIME, &totalTime);
        return totalTime;
    }

    // Get response code (call after perform())
    long getResponseCode() const {
        long responseCode = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);
        return responseCode;
    }

    // Static write callback for collecting response data into std::string
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

private:
    CURL* curl_;

    void checkCurlCode(CURLcode code, const std::string& context) {
        if (code != CURLE_OK) {
            throw std::runtime_error("CURL error in " + context + ": " + curl_easy_strerror(code));
        }
    }
};

#endif // CURL_EASY_HANDLE_H
