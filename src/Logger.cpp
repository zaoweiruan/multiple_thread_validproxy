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
LogLevel Logger::fileLevel_ = LogLevel::DEBUG;
LogLevel Logger::consoleLevel_ = LogLevel::INFO;

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
    
    if (static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }
    
    if (static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
        *outFile_ << fullMsg << std::endl;
        outFile_->flush();
    }
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
    
    if (static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }
    
    if (static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
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
    fileLevel_ = level;
    consoleLevel_ = level;
}

void Logger::setFileLevel(LogLevel level) {
    fileLevel_ = level;
}

void Logger::setConsoleLevel(LogLevel level) {
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

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::LOG_ERROR: return "ERROR";
        default: return "INFO";
    }
}

LogLevel Logger::stringToLevel(const std::string& str) {
    std::string s = str;
    for (auto& c : s) c = std::tolower(c);
    
    if (s == "trace") return LogLevel::TRACE;
    if (s == "debug") return LogLevel::DEBUG;
    if (s == "info") return LogLevel::INFO;
    if (s == "warn" || s == "warning") return LogLevel::WARN;
    if (s == "error") return LogLevel::LOG_ERROR;
    return LogLevel::INFO;
}