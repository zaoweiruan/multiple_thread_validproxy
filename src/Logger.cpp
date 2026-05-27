#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

std::string Logger::logDir_;
std::string Logger::prefix_;
std::ofstream* Logger::outFile_ = nullptr;
std::mutex Logger::mutex_;
bool Logger::enabled_ = false;
bool Logger::fileEnabled_ = true;
bool Logger::consoleEnabled_ = true;
LogLevel Logger::fileLevel_ = LogLevel::DEBUG;
LogLevel Logger::consoleLevel_ = LogLevel::INFO;
Logger::LogCallback Logger::logCallback_ = nullptr;
std::mutex Logger::callbackMutex_;
std::vector<Logger::LogCallback> Logger::callbackStack_;  // 回调栈

void Logger::init(const std::string& logDir, const std::string& prefix) {
    init(logDir, prefix, LogLevel::DEBUG, LogLevel::INFO);
}

void Logger::init(const std::string& logDir, const std::string& prefix, LogLevel fileLevel, LogLevel consoleLevel) {
    std::lock_guard<std::mutex> lock(mutex_);
    logDir_ = logDir;
    prefix_ = prefix;
    fileLevel_ = fileLevel;
    consoleLevel_ = consoleLevel;
    
    std::filesystem::path dir(logDir);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directory(dir);
    }
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&t));
    
    std::string filename = logDir + "/" + prefix + "_" + timestamp + ".log";
    outFile_ = new std::ofstream(filename, std::ios::out | std::ios::trunc);
    enabled_ = outFile_->is_open();
}

void Logger::write(const std::string& msg) {
    write(msg, LogLevel::INFO);
}

void Logger::write(const std::string& msg, LogLevel level) {
    if (!enabled_ || !outFile_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    std::string levelStr = levelToString(level);
    std::string fullMsg = "[" + std::string(timestamp) + "] [" + levelStr + "] " + msg;
    
    if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }
    
    if (fileEnabled_ && static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
        *outFile_ << fullMsg << std::endl;
        outFile_->flush();
    }
    
    // UI callback (invoked under callbackMutex_, not the main mutex, to avoid deadlock)
    {
        std::lock_guard<std::mutex> cbLock(callbackMutex_);
        // logCallback_ == callbackStack_.back() when stack non-empty; else nullptr
        if (logCallback_) {
            logCallback_(fullMsg, level);
        }
    }
}

void Logger::setLogCallback(LogCallback cb) {
    // Initial registration: clear any existing stack entries and set fresh callback
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.clear();
    logCallback_ = std::move(cb);
    if (logCallback_) {
        callbackStack_.push_back(logCallback_);
    }
}

void Logger::clearLogCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.clear();
    logCallback_ = nullptr;
}

void Logger::pushCallback(LogCallback cb) {
    // RAII-style: push a new consumer; it becomes the active callback
    // The previous active caller remains in the stack and will be restored by popCallback()
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.push_back(std::move(cb));
    logCallback_ = callbackStack_.back();
}

void Logger::popCallback() {
    // Remove the top callback and restore the previous one
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (!callbackStack_.empty()) {
        callbackStack_.pop_back();
    }
    logCallback_ = callbackStack_.empty() ? nullptr : callbackStack_.back();
}

void Logger::writeTimestamp(const std::string& msg) {
    writeTimestamp(msg, LogLevel::INFO);
}

void Logger::writeTimestamp(const std::string& msg, LogLevel level) {
    if (!enabled_ || !outFile_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    std::string levelStr = levelToString(level);
    std::string fullMsg = "[" + std::string(timestamp) + "] [" + levelStr + "] " + msg;
    
    if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }
    
    if (fileEnabled_ && static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
        *outFile_ << fullMsg << std::endl;
         outFile_->flush();
    }
}

void Logger::flush() {
    if (outFile_ && outFile_->is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        outFile_->flush();
    }
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outFile_ && outFile_->is_open()) {
        outFile_->close();
    }
    delete outFile_;
    outFile_ = nullptr;
    enabled_ = false;
}

bool Logger::isEnabled() {
    return enabled_;
}

std::ofstream* Logger::getFile() {
    return outFile_;
}

std::string Logger::getLogDir() {
    return logDir_;
}

std::string Logger::getPrefix() {
    return prefix_;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = level;
    consoleLevel_ = level;
}

void Logger::setFileLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = level;
}

void Logger::setConsoleLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleLevel_ = level;
}

LogLevel Logger::getLevel() {
    return fileLevel_;
}

LogLevel Logger::getFileLevel() {
    return fileLevel_;
}

LogLevel Logger::getConsoleLevel() {
    return consoleLevel_;
}

void Logger::setFileEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileEnabled_ = enabled;
}

void Logger::setConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

bool Logger::isFileEnabled() {
    return fileEnabled_;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERR: return "ERROR";
        case LogLevel::REPORT: return "REPORT";
        default: return "INFO";
    }
}

LogLevel Logger::stringToLevel(const std::string& str) {
    std::string s = str;
    for (auto& c : s) c = std::tolower(c);
    
    // Remove "log_" prefix if present (e.g., "log_error" → "error")
    if (s.size() > 4 && s.substr(0, 4) == "log_") {
        s = s.substr(4);
    }
    
    if (s == "trace") return LogLevel::TRACE;
    if (s == "debug") return LogLevel::DEBUG;
    if (s == "info") return LogLevel::INFO;
    if (s == "warn" || s == "warning") return LogLevel::WARN;
    if (s == "error") return LogLevel::ERR;
    if (s == "report") return LogLevel::REPORT;
    return LogLevel::INFO;
}

void Logger::disableFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = static_cast<LogLevel>(100);
}

void Logger::enableConsoleOnly() {
    // 不再禁用文件日志，仅保留控制台输出
    // 如需禁用文件，使用 setFileEnabled(false)
}