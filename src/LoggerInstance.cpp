#include "LoggerInstance.h"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

LoggerInstance::LoggerInstance()
    : outFile_(nullptr)
    , enabled_(false)
    , fileEnabled_(true)
    , consoleEnabled_(true)
    , fileLevel_(LogLevel::DEBUG)
    , consoleLevel_(LogLevel::INFO)
    , logCallback_(nullptr)
{
}

LoggerInstance::~LoggerInstance() {
    close();
}

void LoggerInstance::init(const std::string& logDir, const std::string& prefix) {
    init(logDir, prefix, LogLevel::DEBUG, LogLevel::INFO);
}

void LoggerInstance::init(const std::string& logDir, const std::string& prefix, LogLevel fileLevel, LogLevel consoleLevel) {
    std::lock_guard<std::mutex> lock(mutex_);
    logDir_ = logDir;
    prefix_ = prefix;
    fileLevel_ = fileLevel;
    consoleLevel_ = consoleLevel;

    std::filesystem::path dir(logDir);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directory(dir);
    }

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&t));

    std::string filename = logDir + "/" + prefix + "_" + timestamp + ".log";
    outFile_ = new std::ofstream(filename, std::ios::out | std::ios::trunc);
    enabled_ = outFile_->is_open();
}

void LoggerInstance::write(const std::string& msg) {
    write(msg, LogLevel::INFO);
}

void LoggerInstance::write(const std::string& msg, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));

    std::string levelStr = levelToString(level);
    std::string fullMsg = "[" + std::string(timestamp) + "] [" + levelStr + "] " + msg;

    if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }

    if (fileEnabled_ && outFile_ && outFile_->is_open() && static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
        *outFile_ << fullMsg << std::endl;
        outFile_->flush();
    }

    {
        std::lock_guard<std::mutex> cbLock(callbackMutex_);
        if (logCallback_) {
            logCallback_(fullMsg, level);
        }
    }
}

void LoggerInstance::writeTimestamp(const std::string& msg) {
    writeTimestamp(msg, LogLevel::INFO);
}

void LoggerInstance::writeTimestamp(const std::string& msg, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));

    std::string levelStr = levelToString(level);
    std::string fullMsg = "[" + std::string(timestamp) + "] [" + levelStr + "] " + msg;

    if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
        std::cout << fullMsg << std::endl;
    }

    if (fileEnabled_ && outFile_ && outFile_->is_open() && static_cast<int>(level) >= static_cast<int>(fileLevel_)) {
        *outFile_ << fullMsg << std::endl;
        outFile_->flush();
    }

    {
        std::lock_guard<std::mutex> cbLock(callbackMutex_);
        if (logCallback_) {
            logCallback_(fullMsg, level);
        }
    }
}

void LoggerInstance::flush() {
    if (outFile_ && outFile_->is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        outFile_->flush();
    }
}

void LoggerInstance::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outFile_ && outFile_->is_open()) {
        outFile_->close();
    }
    delete outFile_;
    outFile_ = nullptr;
    enabled_ = false;
}

bool LoggerInstance::isEnabled() const {
    return enabled_;
}

std::ofstream* LoggerInstance::getFile() {
    return outFile_;
}

std::string LoggerInstance::getLogDir() const {
    return logDir_;
}

std::string LoggerInstance::getPrefix() const {
    return prefix_;
}

void LoggerInstance::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = level;
    consoleLevel_ = level;
}

void LoggerInstance::setFileLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = level;
}

void LoggerInstance::setConsoleLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleLevel_ = level;
}

LogLevel LoggerInstance::getLevel() const {
    return fileLevel_;
}

LogLevel LoggerInstance::getFileLevel() const {
    return fileLevel_;
}

LogLevel LoggerInstance::getConsoleLevel() const {
    return consoleLevel_;
}

void LoggerInstance::setFileEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileEnabled_ = enabled;
}

void LoggerInstance::setConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

bool LoggerInstance::isFileEnabled() const {
    return fileEnabled_;
}

void LoggerInstance::setLogCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.clear();
    logCallback_ = std::move(cb);
    if (logCallback_) {
        callbackStack_.push_back(logCallback_);
    }
}

void LoggerInstance::clearLogCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.clear();
    logCallback_ = nullptr;
}

void LoggerInstance::pushCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.push_back(std::move(cb));
    logCallback_ = callbackStack_.back();
}

void LoggerInstance::popCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (!callbackStack_.empty()) {
        callbackStack_.pop_back();
    }
    logCallback_ = callbackStack_.empty() ? nullptr : callbackStack_.back();
}

std::string LoggerInstance::levelToString(LogLevel level) {
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

LogLevel LoggerInstance::stringToLevel(const std::string& str) {
    std::string s = str;
    for (char& c : s) c = std::tolower(c);

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

void LoggerInstance::disableFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    fileLevel_ = static_cast<LogLevel>(100);
}

void LoggerInstance::enableConsoleOnly() {
}
