#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <random>

#include "Logger.h"

// ============================================================
// TempDir — RAII temporary directory for file I/O tests
// ============================================================
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        const char* hex = "0123456789abcdef";
        std::string suffix;
        for (int i = 0; i < 8; ++i) suffix += hex[dis(gen)];

        std::string base = std::filesystem::temp_directory_path().string();
        path_ = base + "/test_tmp_" + suffix;

        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::string& path() const { return path_; }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;

private:
    std::string path_;
};

// ============================================================
// LogCapture — RAII capture of Logger output via callback stack
//              Captures messages only during its lifetime.
// ============================================================
class LogCapture {
public:
    LogCapture() {
        Logger::pushCallback([this](const std::string& msg, LogLevel level) {
            std::lock_guard<std::mutex> lock(mtx_);
            entries_.push_back({msg, level});
        });
    }

    ~LogCapture() {
        Logger::popCallback();
    }

    struct Entry {
        std::string message;
        LogLevel level;
    };

    std::vector<Entry> entries() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return entries_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return entries_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return entries_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        entries_.clear();
    }

    bool contains(const std::string& substr) const {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : entries_) {
            if (e.message.find(substr) != std::string::npos) return true;
        }
        return false;
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

private:
    mutable std::mutex mtx_;
    std::vector<Entry> entries_;
};

#endif // TEST_UTILS_H
