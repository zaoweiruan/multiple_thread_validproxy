#include "XrayInstance.h"
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>

XrayInstance::XrayInstance(const std::string& xrayPath, int socksPort, int apiPort, const std::string& configDir)
    : xrayPath_(xrayPath), socksPort_(socksPort), apiPort_(apiPort),
      stdoutFile_(INVALID_HANDLE_VALUE), stderrFile_(INVALID_HANDLE_VALUE),
      running_(false), lastExitCode_(STILL_ACTIVE) {
    
    configPath_ = configDir + "/xray_config_" + std::to_string(socksPort) + ".json";
    stdoutLogPath_ = configDir + "/xray_stdout_" + std::to_string(socksPort) + ".log";
    stderrLogPath_ = configDir + "/xray_stderr_" + std::to_string(socksPort) + ".log";
    processHandle_ = nullptr;
    jobObject_ = nullptr;
}

XrayInstance::~XrayInstance() {
    stop();
}

bool XrayInstance::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (running_) {
        Logger::write("[XrayInstance] start() called while already running, socks="
                      + std::to_string(socksPort_), LogLevel::WARN);
        return true;
    }

    Logger::write("[XrayInstance] Creating config: " + configPath_, LogLevel::INFO);
    if (!createConfigFile()) {
        Logger::write("[XrayInstance] Failed to create config file", LogLevel::ERR);
        return false;
    }
    
    jobObject_ = CreateJobObjectA(NULL, NULL);
    if (!jobObject_) {
        Logger::write("[XrayInstance] Failed to create job object: " + std::to_string(GetLastError()), LogLevel::ERR);
        return false;
    }
    
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit = {};
    jobLimit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &jobLimit, sizeof(jobLimit))) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] SetInformationJobObject FAILED, err=" + std::to_string(err)
                      + ". Fallback: direct TerminateProcess path.", LogLevel::ERR);
        // KILL_ON_JOB_CLOSE not set — do not rely on close-handle kill.
        // Fallback: if process was already created we would TerminateProcess it in stop().
        // For now, keep jobObject_ so stop() can take the fallback branch.
        // Note: AssignProcessToJobObject with non-kill-on-close job will survive CloseHandle.
    }
    
    // Use CreateProcessW to avoid CreateProcessA's command-line parsing issues
    // with embedded quotes in exe paths. Convert ANSI paths to UTF-16 wide strings.
    int exeWLen = static_cast<int>(MultiByteToWideChar(CP_UTF8, 0, xrayPath_.c_str(), -1, nullptr, 0));
    std::vector<wchar_t> exeW(exeWLen);
    MultiByteToWideChar(CP_UTF8, 0, xrayPath_.c_str(), -1, exeW.data(), exeWLen);

    int cmdWLen = static_cast<int>(MultiByteToWideChar(CP_UTF8, 0,
        ("run -c \"" + configPath_ + "\"").c_str(), -1, nullptr, 0));
    std::vector<wchar_t> cmdW(cmdWLen);
    MultiByteToWideChar(CP_UTF8, 0,
        ("run -c \"" + configPath_ + "\"").c_str(), -1, cmdW.data(), cmdWLen);

    Logger::write("[XrayInstance] Executing: " + xrayPath_ + " run -c " + configPath_, LogLevel::INFO);

    // Redirect the child's stdout/stderr into per-instance log files so that
    // xray warnings and panic stacks survive (previously no std handles were
    // inherited, so xray's stderr output was silently discarded).
    if (!openRedirectFiles()) {
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutFile_;
    si.hStdError = stderrFile_;

    // xray does not read stdin; give it an inheritable NUL handle so the child
    // never inherits an invalid stdin.
    SECURITY_ATTRIBUTES saInherit = {};
    saInherit.nLength = sizeof(saInherit);
    saInherit.bInheritHandle = TRUE;
    HANDLE nulIn = CreateFileA("NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &saInherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    si.hStdInput = (nulIn != INVALID_HANDLE_VALUE) ? nulIn : nullptr;

    PROCESS_INFORMATION pi = {};

    BOOL created = CreateProcessW(exeW.data(), cmdW.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (nulIn != INVALID_HANDLE_VALUE) {
        CloseHandle(nulIn);
    }
    if (!created) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] Failed to create process: " + std::to_string(err), LogLevel::ERR);
        closeRedirectFiles();
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    if (!AssignProcessToJobObject(jobObject_, pi.hProcess)) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] Failed to assign to job: " + std::to_string(err), LogLevel::ERR);
        // The child was created with CREATE_SUSPENDED and never resumed: terminate it now,
        // otherwise it would remain a permanently suspended orphan outside job management.
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        closeRedirectFiles();
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    ResumeThread(pi.hThread);
    processHandle_ = pi.hProcess;
    lastExitCode_ = STILL_ACTIVE;
    CloseHandle(pi.hThread);

    // Short spawn-grace poll: confirm the process survived its first moments
    // (e.g. missing DLL, bad executable path, instant config panic) and log the
    // death tail on failure. This is intentionally SHORT — it is NOT the success
    // criterion. The real "is the instance usable" check (API port ready, while
    // the process is still alive) is performed by XrayManager::waitInstanceReady,
    // which is crash-aware and bounded so a startup flash-crash never blocks for
    // the full timeout. Keeping this grace short avoids wasting seconds on every
    // successful startup (the old code always blocked 5000ms here).
    const int LIVENESS_POLL_MS = 500;
    const int LIVENESS_STEP_MS = 50;
    for (int elapsed = 0; elapsed < LIVENESS_POLL_MS; elapsed += LIVENESS_STEP_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(LIVENESS_STEP_MS));
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(processHandle_, &exitCode) || exitCode != STILL_ACTIVE) {
            logDeathDetails(exitCode);
            TerminateProcess(processHandle_, 1);
            WaitForSingleObject(processHandle_, 2000);
            CloseHandle(processHandle_);
            processHandle_ = nullptr;
            closeRedirectFiles();
            CloseHandle(jobObject_);
            jobObject_ = nullptr;
            running_.store(false);
            return false;
        }
    }

    // Final confirmation: process is alive after the poll window
    DWORD finalExitCode = 0;
    if (!GetExitCodeProcess(processHandle_, &finalExitCode) || finalExitCode != STILL_ACTIVE) {
        logDeathDetails(finalExitCode);
        TerminateProcess(processHandle_, 1);
        WaitForSingleObject(processHandle_, 2000);
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
        closeRedirectFiles();
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        running_.store(false);
        return false;
    }

    running_.store(true);
    Logger::write("[XrayInstance] Started successfully, socks=" + std::to_string(socksPort_) + ", api=" + std::to_string(apiPort_), LogLevel::INFO);
    return true;
}

void XrayInstance::stop() {
    HANDLE job = nullptr;
    HANDLE proc = nullptr;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (jobObject_ == nullptr && processHandle_ == nullptr) {
            Logger::write("[XrayInstance][stop] no process handle (already stopped?)", LogLevel::INFO);
            return;
        }
        // Detach handles under the lock so repeated/concurrent stop() can never
        // double-CloseHandle; the blocking termination runs outside the lock.
        running_.store(false);
        job = jobObject_;
        jobObject_ = nullptr;
        proc = processHandle_;
        processHandle_ = nullptr;
    }

    // Force kill the process tree using the job object
    if (job) {
        Logger::write("[XrayInstance][stop] TerminateJobObject, socks="
                      + std::to_string(socksPort_) + " api="
                      + std::to_string(apiPort_), LogLevel::INFO);
        TerminateJobObject(job, 1);
        // Graceful wait: give processes up to GRACEFUL_SHUTDOWN_MS to drain before Job close
        std::this_thread::sleep_for(std::chrono::milliseconds(GRACEFUL_SHUTDOWN_MS));
    }
    if (proc) {
        // Wait synchronously: process may have already exited or been killed by the Job above.
        // WAIT_TIMEOUT here does NOT necessarily mean the process is alive — it only means
        // GRACEFUL_SHUTDOWN_MS elapsed; the exit will be confirmed by GetExitCodeProcess below.
        DWORD waitResult = WaitForSingleObject(proc, GRACEFUL_SHUTDOWN_MS);
        DWORD exitCode = 0;
        GetExitCodeProcess(proc, &exitCode);
        if (exitCode == STILL_ACTIVE) {
            // Grace window elapsed and the process is still alive: force-kill it so the
            // process does not survive stop() with ports still bound.
            Logger::write("[XrayInstance][stop] still active after grace, TerminateProcess, socks="
                          + std::to_string(socksPort_), LogLevel::ERR);
            TerminateProcess(proc, 1);
            WaitForSingleObject(proc, GRACEFUL_SHUTDOWN_MS);
            GetExitCodeProcess(proc, &exitCode);
        }
        bool exited = (exitCode != STILL_ACTIVE);

        std::string reason = exited
            ? "exit=" + std::to_string(exitCode)
            : "exit, WAIT_TIMEOUT=" + std::to_string(waitResult)
                    + " (terminated by Job object, exitCode=" + std::to_string(exitCode) + ")";

        Logger::write("[XrayInstance][stop] "
                      + std::string(exited ? "exited" : "exited by Job") + ", "
                      + reason + ", socks=" + std::to_string(socksPort_),
                      LogLevel::INFO);
        CloseHandle(proc);
    }
    if (job) {
        CloseHandle(job);
    }
    closeRedirectFiles();
}

bool XrayInstance::isRunning() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (processHandle_ == nullptr) {
        return false;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(processHandle_, &exitCode)) {
        return false;
    }
    bool alive = (exitCode == STILL_ACTIVE);
    if (!alive) {
        running_.store(false);
        // Log exit code + stderr tail once per death (transition detection).
        if (lastExitCode_ == STILL_ACTIVE) {
            logDeathDetails(exitCode);
        } else {
            lastExitCode_ = exitCode;
        }
    }
    return alive;
}

DWORD XrayInstance::lastExitCode() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastExitCode_;
}

int XrayInstance::getSocksPort() const {
    return socksPort_;
}

int XrayInstance::getApiPort() const {
    return apiPort_;
}

DWORD XrayInstance::getPid() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (processHandle_ == nullptr) {
        return 0;
    }
    return GetProcessId(processHandle_);
}

std::string XrayInstance::getConfigPath() const {
    return configPath_;
}

bool XrayInstance::createConfigFile() {
    // StandaloneProxyPool mode: write the prebuilt pool config verbatim.
    if (!explicitConfig_.empty()) {
        std::ofstream out(configPath_);
        if (!out.is_open()) return false;
        out << explicitConfig_;
        out.close();
        return true;
    }

    // Ensure the config directory exists
    std::filesystem::path configPath(configPath_);
    std::error_code ec;
    if (!std::filesystem::exists(configPath.parent_path()) && 
        !std::filesystem::create_directories(configPath.parent_path(), ec)) {
        Logger::write("[XrayInstance] Failed to create config directory: " + 
                     configPath.parent_path().string() + " (err=" + ec.message() + ")", LogLevel::ERR);
        return false;
    }

    std::string content = R"({
        "log": {"loglevel": "warning"},
        "api": {
            "tag": "api",
            "services": ["HandlerService", "LoggerService", "StatsService", "RoutingService"]
        },
        "stats": {},
        "policy": {
            "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
            "system": {"statsInboundUplink": true, "statsInboundDownlink": true, "statsOutboundUplink": true, "statsOutboundDownlink": true}
        },
        "inbounds": [
            {"tag": "api", "listen": "127.0.0.1", "port": )" + std::to_string(apiPort_) + R"(, "protocol": "dokodemo-door", "settings": {"address": "127.0.0.1"}},
            {"tag": "socks-in", "listen": "127.0.0.1", "port": )" + std::to_string(socksPort_) + R"(, "protocol": "mixed", "settings": {"auth": "noauth", "udp": true}}
        ],
        "outbounds": [
            {"tag": "direct", "protocol": "freedom"},
            {"tag": "proxy", "protocol": "freedom"}
        ],
        "routing": {
            "domainStrategy": "AsIs",
            "rules": [
                {"type": "field", "inboundTag": ["api"], "outboundTag": "api"},
                {"type": "field", "outboundTag": "proxy", "network": "tcp"}
            ]
        }
    })";
    
    std::ofstream out(configPath_);
    if (!out.is_open()) return false;
    out << content;
    out.close();
    return true;
}

void XrayInstance::setExplicitConfig(const std::string& configJson) {
    explicitConfig_ = configJson;
}

bool XrayInstance::openRedirectFiles() {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    stdoutFile_ = CreateFileA(stdoutLogPath_.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    stderrFile_ = CreateFileA(stderrLogPath_.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stdoutFile_ == INVALID_HANDLE_VALUE || stderrFile_ == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] Failed to open redirect log files: "
                      + std::to_string(err), LogLevel::ERR);
        closeRedirectFiles();
        return false;
    }
    return true;
}

void XrayInstance::closeRedirectFiles() {
    if (stdoutFile_ != INVALID_HANDLE_VALUE && stdoutFile_ != nullptr) {
        CloseHandle(stdoutFile_);
        stdoutFile_ = INVALID_HANDLE_VALUE;
    }
    if (stderrFile_ != INVALID_HANDLE_VALUE && stderrFile_ != nullptr) {
        CloseHandle(stderrFile_);
        stderrFile_ = INVALID_HANDLE_VALUE;
    }
}

void XrayInstance::logDeathDetails(DWORD exitCode) const {
    lastExitCode_ = exitCode;
    std::string msg = "[XrayInstance] Process exited unexpectedly (socks="
                      + std::to_string(socksPort_) + ", exitCode="
                      + std::to_string(exitCode) + ")";
    // Xray prints fatal "Failed to start" errors to stdout, not stderr, so we
    // surface both tails — otherwise the real cause (e.g. "unable to listen on
    // 127.0.0.1:10809") never reaches the application log.
    std::string stderrTail = readFileTail(stderrLogPath_, DEATH_STDERR_TAIL_BYTES);
    std::string stdoutTail = readFileTail(stdoutLogPath_, DEATH_STDERR_TAIL_BYTES);
    if (!stderrTail.empty()) {
        msg += ", stderr tail:\n" + stderrTail;
    }
    if (!stdoutTail.empty()) {
        msg += ", stdout tail:\n" + stdoutTail;
    }
    Logger::write(msg, LogLevel::ERR);
}

std::string XrayInstance::readFileTail(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::string();
    }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size <= 0) {
        return std::string();
    }
    size_t readLen = (static_cast<size_t>(size) <= maxBytes)
                         ? static_cast<size_t>(size)
                         : maxBytes;
    in.seekg(-static_cast<std::streamoff>(readLen), std::ios::end);
    std::string content(readLen, '\0');
    in.read(&content[0], static_cast<std::streamsize>(readLen));
    // The window covers the whole file: there is no partial leading line,
    // so keep everything (only strip one trailing line break for tidiness).
    if (readLen >= static_cast<size_t>(size)) {
        while (!content.empty() &&
               (content.back() == '\n' || content.back() == '\r')) {
            content.pop_back();
        }
        return content;
    }
    // The window cut off the file head: drop the first (possibly partial)
    // line so the tail never starts mid-line.
    size_t firstNewline = content.find('\n');
    if (firstNewline != std::string::npos && firstNewline != 0) {
        content = content.substr(firstNewline + 1);
    }
    return content;
}