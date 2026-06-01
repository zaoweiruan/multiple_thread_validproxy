#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <windows.h>
#include "XrayApi.h"
#include "Logger.h"

namespace xray {

static std::string normalizePath(const std::string& path) {
    std::string result = path;
    for (char& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

// -------------------------------------------------------------------
// Helper: run a process with CREATE_NO_WINDOW and capture stdout,
// optionally providing stdin data (e.g., piping JSON into xray api).
// Returns exit code (negative on creation failure).
// -------------------------------------------------------------------
static int runProcess(const std::string& cmd, std::string& output,
                      const std::string* stdinData = nullptr) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    // Create stdout pipe (child writes, parent reads)
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0))
        return -1;
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    // Create stdin pipe only when input is provided
    HANDLE hInRead = nullptr, hInWrite = nullptr;
    if (stdinData) {
        if (!CreatePipe(&hInRead, &hInWrite, &sa, 0)) {
            CloseHandle(hOutRead); CloseHandle(hOutWrite);
            return -1;
        }
        SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si = {0};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESTDHANDLES;
    si.hStdOutput   = hOutWrite;
    si.hStdError    = hOutWrite;  // merge stderr with stdout
    si.hStdInput    = stdinData ? hInRead : INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi = {0};

    // CreateProcessA modifies the command line buffer
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                                  TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                  &si, &pi);

    // Parent no longer needs the child-side write/stdin ends
    CloseHandle(hOutWrite);
    if (hInRead) CloseHandle(hInRead);

    if (!created) {
        CloseHandle(hOutRead);
        if (hInWrite) CloseHandle(hInWrite);
        return -1;
    }

    // Write stdin data if requested (pipe JSON to xray api ado)
    if (stdinData && hInWrite) {
        DWORD written = 0;
        WriteFile(hInWrite, stdinData->c_str(), (DWORD)stdinData->size(),
                  &written, nullptr);
        CloseHandle(hInWrite);
    }

    // Read all stdout output
    char buf[4096];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hOutRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) &&
           bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, 5000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hOutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

XrayApi::XrayApi(const std::string& xrayPath, const std::string& serverAddr)
    : xrayPath_(normalizePath(xrayPath)), serverAddr_(serverAddr) {}

bool XrayApi::runCommand(const std::string& args, std::string& output) {
    std::string cmd = "\"" + xrayPath_ + "\" " + args;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run command: " + cmd;
        return false;
    }
    return exitCode == 0;
}

bool XrayApi::addOutbound(const std::string& outboundJson, const std::string& tag, std::string& resultOutput) {
    Logger::write("[XrayApi] addOutbound called: tag=" + tag + ", xrayPath=" + xrayPath_ + ", serverAddr=" + serverAddr_, LogLevel::DEBUG);
    Logger::write("[XrayApi] outbound JSON: " + outboundJson, LogLevel::DEBUG);
    
    std::string normalizedXray = xrayPath_;
    for (char& c : normalizedXray) {
        if (c == '/') c = '\\';
    }
    
    std::string normalizedServer = serverAddr_;
    for (char& c : normalizedServer) {
        if (c == '/') c = '\\';
    }
    
    // Run xray api ado with JSON as stdin (no cmd.exe wrapper, no console flash)
    std::string cmd = "\"" + normalizedXray + "\" api ado --server=" + normalizedServer + " stdin:";
    Logger::write("[XrayApi] command: " + cmd, LogLevel::DEBUG);

    std::string output;
    int exitCode = runProcess(cmd, output, &outboundJson);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api ado command";
        return false;
    }

    resultOutput = output;

    bool success = (exitCode == 0) || (output.find("adding") != std::string::npos);
    
    if (!success) {
        lastError_ = "xray api ado failed with code: " + std::to_string(exitCode) + " output: " + output;
        Logger::write("[XrayApi] addOutbound FAILED: exitCode=" + std::to_string(exitCode) + ", output=" + output, LogLevel::ERR);
        Logger::write("[XrayApi] addOutbound FAILED error: " + lastError_, LogLevel::ERR);
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Logger::write("[XrayApi] addOutbound SUCCESS for tag: " + tag, LogLevel::DEBUG);

    // List outbounds after add to confirm
    std::string lsoCmd = "\"" + xrayPath_ + "\" api lso --server=" + serverAddr_;
    std::string lsoOutput;
    int lsoCode = runProcess(lsoCmd, lsoOutput);
    if (lsoCode >= 0 && !lsoOutput.empty()) {
        resultOutput += "\n[Outbounds]:\n" + lsoOutput;
    }

    return true;
}

bool XrayApi::removeOutbound(const std::string& tag) {
    Logger::write("[XrayApi] removeOutbound called: tag=" + tag, LogLevel::DEBUG);
    
    std::string cleanTag;
    for (char c : tag) {
        if (c >= 'a' && c <= 'z') cleanTag += c;
        else if (c >= 'A' && c <= 'Z') cleanTag += c;
        else if (c >= '0' && c <= '9') cleanTag += c;
        else if (c == '_' || c == '-') cleanTag += c;
    }
    if (cleanTag.empty()) cleanTag = "proxy";
    
    Logger::write("[XrayApi] removeOutbound: cleaned tag=" + cleanTag + ", cmd tag=\"" + cleanTag + "\"", LogLevel::DEBUG);
    
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

    Logger::write("[XrayApi] removeOutbound SUCCESS for tag: " + tag, LogLevel::DEBUG);

    return true;
}

bool XrayApi::listOutbounds() {
    std::string cmd = "\"" + xrayPath_ + "\" api lso --server " + serverAddr_;

    std::string output;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api lso command";
        return false;
    }

    // Log output for debugging
    if (!output.empty())
        Logger::write("[XrayApi] listOutbounds:\n" + output, LogLevel::DEBUG);

    return exitCode == 0;
}

bool XrayApi::ping(std::string& resultOutput) {
    std::string cmd = "\"" + xrayPath_ + "\" api lsi --server " + serverAddr_;

    std::string output;
    int exitCode = runProcess(cmd, output);
    if (exitCode < 0) {
        lastError_ = "Failed to run xray api lsi command";
        return false;
    }

    resultOutput = output;
    return exitCode == 0;
}

std::string XrayApi::getLastError() const {
    return lastError_;
}

} // namespace xray
