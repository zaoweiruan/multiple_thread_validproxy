#include "XrayInstance.h"
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

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
    
    std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + configPath_ + "\"";
    Logger::write("[XrayInstance] Executing: " + cmd, LogLevel::INFO);
    
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    
    BOOL created = CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!created) {
        DWORD err = GetLastError();
        Logger::write("[XrayInstance] Failed to create process: " + std::to_string(err), LogLevel::ERR);
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    if (!AssignProcessToJobObject(jobObject_, pi.hProcess)) {
        Logger::write("[XrayInstance] Failed to assign to job: " + std::to_string(GetLastError()), LogLevel::ERR);
        // Do NOT resume the suspended process — it would become an unmanaged orphan.
        // Caller will check start() return value and handle cleanup.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }
    
    ResumeThread(pi.hThread);
    processHandle_ = pi.hProcess;
    CloseHandle(pi.hThread);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    running_ = true;
    Logger::write("[XrayInstance] Started successfully, socks=" + std::to_string(socksPort_) + ", api=" + std::to_string(apiPort_), LogLevel::INFO);
    return true;
}

void XrayInstance::stop() {
    // Force kill the process tree using the job object
    if (jobObject_) {
        Logger::write("[XrayInstance][stop] TerminateJobObject, socks="
                      + std::to_string(socksPort_) + " api="
                      + std::to_string(apiPort_), LogLevel::INFO);
        TerminateJobObject(jobObject_, 1);
        // Graceful wait: give processes up to GRACEFUL_SHUTDOWN_MS to drain before Job close
        std::this_thread::sleep_for(std::chrono::milliseconds(GRACEFUL_SHUTDOWN_MS));
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
    }
    if (processHandle_) {
        // Wait synchronously: process may have already exited or been killed by the Job above.
        // WAIT_TIMEOUT here does NOT necessarily mean the process is alive — it only means
        // GRACEFUL_SHUTDOWN_MS elapsed; the exit will be confirmed by GetExitCodeProcess below.
        DWORD waitResult = WaitForSingleObject(processHandle_, GRACEFUL_SHUTDOWN_MS);
        DWORD exitCode = 0;
        GetExitCodeProcess(processHandle_, &exitCode);
        bool exited = (exitCode != STILL_ACTIVE);

        LogLevel lvl = exited ? LogLevel::INFO : LogLevel::INFO;
        std::string reason = exited
            ? "exit=" + std::to_string(exitCode)
            : "exit, WAIT_TIMEOUT=" + std::to_string(waitResult)
                    + " (terminated by Job object, exitCode=" + std::to_string(exitCode) + ")";

        Logger::write("[XrayInstance][stop] "
                      + std::string(exited ? "exited" : "exited by Job") + ", "
                      + reason + ", socks=" + std::to_string(socksPort_),
                      lvl);
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    } else {
        Logger::write("[XrayInstance][stop] no process handle (already stopped?)", LogLevel::INFO);
    }
    running_ = false;
}

bool XrayInstance::isRunning() const {
    return running_;
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