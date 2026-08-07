#include "XrayInstance.h"
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>

XrayInstance::XrayInstance(const std::string& xrayPath, int socksPort, int apiPort, const std::string& configDir)
    : xrayPath_(xrayPath), socksPort_(socksPort), apiPort_(apiPort), running_(false) {
    
    configPath_ = configDir + "/xray_config_" + std::to_string(socksPort) + ".json";
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

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL created = CreateProcessW(exeW.data(), cmdW.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!created) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] Failed to create process: " + std::to_string(err), LogLevel::ERR);
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
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    ResumeThread(pi.hThread);
    processHandle_ = pi.hProcess;
    CloseHandle(pi.hThread);

    // Bounded liveness poll: verify the process survived for at least a few hundred ms
    // rather than blindly waiting 2s then unconditionally declaring success.
    const int LIVENESS_POLL_MS = 5000;
    const int LIVENESS_STEP_MS = 100;
    for (int elapsed = 0; elapsed < LIVENESS_POLL_MS; elapsed += LIVENESS_STEP_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(LIVENESS_STEP_MS));
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(processHandle_, &exitCode) || exitCode != STILL_ACTIVE) {
            Logger::write("[XrayInstance] Process died during startup (exitCode="
                          + std::to_string(exitCode) + "), socks="
                          + std::to_string(socksPort_), LogLevel::ERR);
            TerminateProcess(processHandle_, 1);
            WaitForSingleObject(processHandle_, 2000);
            CloseHandle(processHandle_);
            processHandle_ = nullptr;
            CloseHandle(jobObject_);
            jobObject_ = nullptr;
            running_.store(false);
            return false;
        }
    }

    // Final confirmation: process is alive after the poll window
    DWORD finalExitCode = 0;
    if (!GetExitCodeProcess(processHandle_, &finalExitCode) || finalExitCode != STILL_ACTIVE) {
        Logger::write("[XrayInstance] Process not alive after poll, socks="
                      + std::to_string(socksPort_), LogLevel::ERR);
        TerminateProcess(processHandle_, 1);
        WaitForSingleObject(processHandle_, 2000);
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
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
}

bool XrayInstance::isRunning() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return running_.load();
}

int XrayInstance::getSocksPort() const {
    return socksPort_;
}

int XrayInstance::getApiPort() const {
    return apiPort_;
}

std::string XrayInstance::getConfigPath() const {
    return configPath_;
}

bool XrayInstance::createConfigFile() {
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
            "services": ["HandlerService", "LoggerService", "StatsService"]
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
        "outbounds": [{"tag": "direct", "protocol": "freedom"}],
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