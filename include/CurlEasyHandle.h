#ifndef CURL_EASY_HANDLE_H
#define CURL_EASY_HANDLE_H

#include <curl/curl.h>
#include <string>
#include <stdexcept>
#include <atomic>
#include <mutex>

/// Process-wide shared libcurl DNS cache.
///
/// Every CurlEasyHandle attaches to this share object and sets
/// CURLOPT_DNS_CACHE_TIMEOUT to -1, so DNS resolutions are cached forever
/// and shared across all handles and threads. The share object is created
/// lazily on first use and intentionally NOT destroyed at exit — the OS
/// reclaims it when the process terminates, so entries stay cached "until
/// the program exits" with zero static-destruction-order risk.
class DnsShareCache {
public:
    /// Returns the process-wide share handle, creating it on first use.
    static CURLSH* get() {
        static CURLSH* s_share = createShare();
        return s_share;
    }

private:
    /// Process-wide mutex guarding the shared DNS cache (required by libcurl
    /// for any CURL_LOCK_DATA_DNS shared across threads).
    static std::mutex& mutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }

    static CURLSH* createShare() {
        CURLSH* share = curl_share_init();
        if (!share) {
            return nullptr;
        }
        if (curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS) != CURLSHE_OK) {
            curl_share_cleanup(share);
            return nullptr;
        }
        if (curl_share_setopt(share, CURLSHOPT_LOCKFUNC, lockCallback) != CURLSHE_OK) {
            curl_share_cleanup(share);
            return nullptr;
        }
        if (curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, unlockCallback) != CURLSHE_OK) {
            curl_share_cleanup(share);
            return nullptr;
        }
        if (curl_share_setopt(share, CURLSHOPT_USERDATA, &mutex()) != CURLSHE_OK) {
            curl_share_cleanup(share);
            return nullptr;
        }
        return share;
    }

    static void lockCallback(CURL* handle, curl_lock_data data,
                             curl_lock_access access, void* userptr) {
        (void)handle; (void)data; (void)access;
        std::mutex* m = static_cast<std::mutex*>(userptr);
        if (m) m->lock();
    }

    static void unlockCallback(CURL* handle, curl_lock_data data, void* userptr) {
        (void)handle; (void)data;
        std::mutex* m = static_cast<std::mutex*>(userptr);
        if (m) m->unlock();
    }
};

class CurlEasyHandle {
public:
    CurlEasyHandle() : curl_(curl_easy_init()) {
        if (!curl_) {
            throw std::runtime_error("curl_easy_init() failed");
        }
        // Attach to the process-wide DNS share cache: DNS resolutions are
        // cached forever (until program exit) and shared across every handle
        // and thread. setopt failures are intentionally ignored so the handle
        // degrades to libcurl's default per-handle DNS behavior.
        CURLSH* share = DnsShareCache::get();
        if (share != nullptr) {
            curl_easy_setopt(curl_, CURLOPT_SHARE, share);
        }
        curl_easy_setopt(curl_, CURLOPT_DNS_CACHE_TIMEOUT, -1L);
    }

    ~CurlEasyHandle() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    CurlEasyHandle(const CurlEasyHandle&) = delete;
    CurlEasyHandle& operator=(const CurlEasyHandle&) = delete;

    CurlEasyHandle(CurlEasyHandle&& other) noexcept : curl_(other.curl_),
        cancelFlag_(other.cancelFlag_), secondaryCancelFlag_(other.secondaryCancelFlag_) {
        other.curl_ = nullptr;
    }

    CurlEasyHandle& operator=(CurlEasyHandle&& other) noexcept {
        if (this != &other) {
            if (curl_) {
                curl_easy_cleanup(curl_);
            }
            curl_ = other.curl_;
            cancelFlag_ = other.cancelFlag_;
            secondaryCancelFlag_ = other.secondaryCancelFlag_;
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
        // E3: CURLOPT_TIMEOUT_MS=0 disables the timeout (unbounded hang).
        // Floor only invalid values (<=0) to 1000ms; keep small positive values
        // so fast network probes (e.g. NetworkMonitor 25/50ms) stay responsive.
        long clampedMs = (ms > 0L) ? ms : 1000L;
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, clampedMs), "setTimeoutMs");
        long connectMs = (clampedMs < 3000L) ? clampedMs : 3000L;
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, connectMs), "setConnectTimeoutMs");
        return *this;
    }

    CurlEasyHandle& setConnectTimeoutMs(long ms) {
        // E3: same policy as setTimeoutMs — only guard 0/negative, do not
        // inflate small explicit connect timeouts (NetworkMonitor uses 25ms).
        long clampedMs = (ms > 0L) ? ms : 1000L;
        checkCurlCode(curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, clampedMs), "setConnectTimeoutMs");
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
        try {
            str->append(contents, size * nmemb);
        } catch (...) {
            return 0;
        }
        return size * nmemb;
    }

    /// Set an atomic flag that, when true, will abort the blocking perform() call
    /// via libcurl's progress callback. Useful for cancellation during batch testing.
    CurlEasyHandle& setCancelFlag(std::atomic<bool>* flag) {
        cancelFlag_ = flag;
        secondaryCancelFlag_ = nullptr;
        return *this;
    }

    /// Set a primary and an optional secondary cancel flag. Either flag becoming
    /// true aborts the blocking perform() call via the progress callback.
    CurlEasyHandle& setCancelFlags(std::atomic<bool>* primary, std::atomic<bool>* secondary = nullptr) {
        cancelFlag_ = primary;
        secondaryCancelFlag_ = secondary;
        return *this;
    }

    void perform() {
        if (cancelFlag_ || secondaryCancelFlag_) {
            // Enable the progress callback so we can abort when cancellation is requested
            checkCurlCode(curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L), "perform(noprogress)");
            checkCurlCode(curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, cancelCallback), "perform(xferinfo)");
            checkCurlCode(curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this), "perform(xferinfodata)");
        }
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
    std::atomic<bool>* cancelFlag_{nullptr};
    std::atomic<bool>* secondaryCancelFlag_{nullptr};

    void checkCurlCode(CURLcode code, const std::string& context) {
        if (code != CURLE_OK) {
            throw std::runtime_error("CURL error in " + context + ": " + curl_easy_strerror(code));
        }
    }

    /// True when any registered cancel flag is set.
    bool isCancelRequested() const {
        if (cancelFlag_ && cancelFlag_->load()) return true;
        if (secondaryCancelFlag_ && secondaryCancelFlag_->load()) return true;
        return false;
    }

    /// libcurl progress callback: returns non-zero to abort the transfer
    /// when any cancel flag is set to true.
    static int cancelCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
        (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
        CurlEasyHandle* self = static_cast<CurlEasyHandle*>(clientp);
        return self->isCancelRequested() ? 1 : 0;
    }
};

#endif // CURL_EASY_HANDLE_H