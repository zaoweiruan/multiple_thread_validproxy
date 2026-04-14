#include "XrayInstance.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

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
    std::cout << "[XrayInstance] Creating config: " << configPath_ << std::endl;
    if (!createConfigFile()) {
        std::cerr << "[XrayInstance] Failed to create config file" << std::endl;
        return false;
    }
    
    jobObject_ = CreateJobObjectA(NULL, NULL);
    if (!jobObject_) {
        std::cerr << "[XrayInstance] Failed to create job object: " << GetLastError() << std::endl;
        return false;
    }
    
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit = {};
    jobLimit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &jobLimit, sizeof(jobLimit));
    
    std::string cmd = "\"" + xrayPath_ + "\" run -c \"" + configPath_ + "\"";
    std::cout << "[XrayInstance] Executing: " << cmd << std::endl;
    
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    
    BOOL created = CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!created) {
        DWORD err = GetLastError();
        std::cerr << "[XrayInstance] Failed to create process: " << err << std::endl;
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
        return false;
    }
    
    if (!AssignProcessToJobObject(jobObject_, pi.hProcess)) {
        std::cerr << "[XrayInstance] Failed to assign to job: " << GetLastError() << std::endl;
    }
    
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    processHandle_ = pi.hProcess;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    running_ = true;
    std::cout << "[XrayInstance] Started successfully, socks=" << socksPort_ << ", api=" << apiPort_ << std::endl;
    return true;
}

void XrayInstance::stop() {
    if (jobObject_) {
        TerminateJobObject(jobObject_, 0);
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
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