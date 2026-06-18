#ifndef CURL_EASY_HANDLE_H
#define CURL_EASY_HANDLE_H

#include <curl/curl.h>
#include <string>
#include <stdexcept>

class CurlEasyHandle {
public:
    CurlEasyHandle() : curl_(curl_easy_init()) {
        if (!curl_) {
            throw std::runtime_error("curl_easy_init() failed");
        }
    }

    ~CurlEasyHandle() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    CurlEasyHandle(const CurlEasyHandle&) = delete;
    CurlEasyHandle& operator=(const CurlEasyHandle&) = delete;

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

    CURL* get() const { return curl_; }

    bool valid() const { return curl_ != nullptr; }

    CurlEasyHandle& setUrl(const std::string& url) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_URL, url.c_str()), "setUrl");
        return *this;
    }

    CurlEasyHandle& setProxy(const std::string& proxyUrl) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_PROXY, proxyUrl.c_str()), "setProxy");
        return *this;
    }

    CurlEasyHandle& setTimeoutMs(long ms) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, ms), "setTimeoutMs");
        return *this;
    }

    CurlEasyHandle& setConnectTimeoutMs(long ms) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, ms), "setConnectTimeoutMs");
        return *this;
    }

    CurlEasyHandle& setTimeoutSec(long sec) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_TIMEOUT, sec), "setTimeoutSec");
        return *this;
    }

    CurlEasyHandle& setNoBody(bool enable = true) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_NOBODY, enable ? 1L : 0L), "setNoBody");
        return *this;
    }

    CurlEasyHandle& setFollowLocation(bool enable = true) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, enable ? 1L : 0L), "setFollowLocation");
        return *this;
    }

    CurlEasyHandle& setSslVerifyPeer(bool enable) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, enable ? 1L : 0L), "setSslVerifyPeer");
        return *this;
    }

    CurlEasyHandle& setSslVerifyHost(bool enable) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, enable ? 1L : 0L), "setSslVerifyHost");
        return *this;
    }

    CurlEasyHandle& setWriteCallback(curl_write_callback cb, void* userData) {
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, cb), "setWriteCallback");
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_WRITEDATA, userData), "setWriteCallback(userData)");
        return *this;
    }

    static size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
        std::string* str = static_cast<std::string*>(userp);
        str->append(contents, size * nmemb);
        return size * nmemb;
    }

    void perform() {
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_perform failed: ") + curl_easy_strerror(res));
        }
    }

    double getTotalTime() const {
        double totalTime = 0.0;
        curl_easy_getinfo(curl_, CURLINFO_TOTAL_TIME, &totalTime);
        return totalTime;
    }

    long getResponseCode() const {
        long responseCode = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);
        return responseCode;
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