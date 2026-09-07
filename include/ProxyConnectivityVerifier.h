#pragma once

#include <functional>
#include <string>

namespace proxy {

// Verifies that a locally started proxy actually tunnels traffic to the test
// URL through its SOCKS port (Spec R2, docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md).
class ConnectivityVerifier {
public:
    using ProbeFn = std::function<bool()>;

    // Probes fetchViaProxy(url, socksPort, ...) up to maxAttempts times.
    // Returns true when a probe succeeds (non-empty response body).
    static bool verify(int socksPort, const std::string& url, int timeoutMs,
                       int maxAttempts = 3);

    // Probes the injected probe function up to maxAttempts times.
    // Returns true as soon as a probe returns true. A failed probe is followed
    // by retryDelayMs before the next attempt, so the total window can cover
    // the proxy process startup time (the xray process needs several seconds
    // before it is fully ready).
    static bool verifyWithProbe(ProbeFn probe, int maxAttempts = 3,
                                int retryDelayMs = 0);

    // Polls the SOCKS port until a TCP connect succeeds or timeoutMs elapses.
    // Used to cover the proxy process startup window (CreateProcessA → port
    // ready) before running the full connectivity probe (Spec R2 §3.3.2).
    static bool waitForPort(int port, int timeoutMs);

    // Crash-aware variant: also bails out early if the backing xray/sing-box
    // process (processHandle) has already exited before the port opened. This
    // prevents a long idle wait on a flash-crash — the "启动闪崩却长时间等待"
    // bug — instead of burning the entire timeout window polling a port that
    // will never open. Pass nullptr to disable the check (HANDLE is void* on
    // Windows, so a bare pointer is used here to avoid leaking windows.h).
    static bool waitForPort(int port, int timeoutMs, void* processHandle);

private:
    ConnectivityVerifier() = delete;
};

} // namespace proxy