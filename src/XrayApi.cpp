#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <windows.h>
#include "XrayApi.h"

namespace xray {

static std::string normalizePath(const std::string& path) {
    std::string result = path;
    for (char& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

XrayApi::XrayApi(const std::string& xrayPath, const std::string& serverAddr)
    : xrayPath_(normalizePath(xrayPath)), serverAddr_(serverAddr) {}

bool XrayApi::runCommand(const std::string& args, std::string& output) {
    std::string cmd = "\"" + xrayPath_ + "\" " + args;
    
    char buffer[4096];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        lastError_ = "Failed to run command: " + cmd;
        return false;
    }
    
    output.clear();
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    int exitCode = _pclose(pipe);
    return exitCode == 0;
}

bool XrayApi::addOutbound(const std::string& outboundJson, const std::string& tag) {
    std::string cmd = "echo " + outboundJson + " | \"" + xrayPath_ + "\" api ado --server " + serverAddr_;

    char buffer[4096];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        lastError_ = "Failed to run xray api ado command";
        return false;
    }

    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int exitCode = _pclose(pipe);

    if (exitCode != 0) {
        lastError_ = "xray api ado failed with code: " + std::to_string(exitCode) + " output: " + output;
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return true;
}

bool XrayApi::removeOutbound(const std::string& tag) {
    std::string cleanTag;
    for (char c : tag) {
        if (c >= 'a' && c <= 'z') cleanTag += c;
        else if (c >= 'A' && c <= 'Z') cleanTag += c;
        else if (c >= '0' && c <= '9') cleanTag += c;
        else if (c == '_' || c == '-') cleanTag += c;
    }
    if (cleanTag.empty()) cleanTag = "proxy";
    
    std::string cmd = "\"" + xrayPath_ + "\" api rmo --server " + serverAddr_ + " \"" + cleanTag + "\"";

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi = {0};

    BOOL success = CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 
                                  CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    
    if (!success) {
        lastError_ = "Failed to create process: " + std::to_string(GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return true;
}

bool XrayApi::listOutbounds() {
    std::string cmd = "\"" + xrayPath_ + "\" api lso --server " + serverAddr_;

    char buffer[4096];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        lastError_ = "Failed to run xray api lso command";
        return false;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::cout << "  " << buffer;
    }
    int exitCode = _pclose(pipe);

    return exitCode == 0;
}

std::string XrayApi::getLastError() const {
    return lastError_;
}

} // namespace xray
