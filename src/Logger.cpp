#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <iomanip>

std::string Logger::logDir_;
std::string Logger::prefix_;
std::ofstream* Logger::outFile_ = nullptr;
std::mutex Logger::mutex_;
bool Logger::enabled_ = false;

void Logger::init(const std::string& logDir, const std::string& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    logDir_ = logDir;
    prefix_ = prefix;
    
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
    if (!enabled_ || !outFile_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    *outFile_ << msg << std::endl;
    outFile_->flush();
}

void Logger::writeTimestamp(const std::string& msg) {
    if (!enabled_ || !outFile_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
    
    *outFile_ << "[" << timestamp << "] " << msg << std::endl;
    outFile_->flush();
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