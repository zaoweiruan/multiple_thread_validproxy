#include "i18n/LogBanner.h"

#include "Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace i18n {
namespace LogBanner {
namespace {

namespace fs = std::filesystem;

std::string normalizePath(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(path, ec);
    if (ec) {
        p = fs::path(path);
    }
    return p.string();
}

}  // namespace

std::string readPreviousLanguage(const std::string& logDir, const std::string& currentLogPath) {
    const std::string current = normalizePath(currentLogPath);

    std::vector<fs::directory_entry> candidates;
    std::error_code ec;
    if (!fs::exists(logDir, ec) || !fs::is_directory(logDir, ec)) {
        return std::string();
    }
    for (const auto& entry : fs::directory_iterator(logDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".log") continue;
        if (normalizePath(entry.path().string()) == current) continue;
        candidates.push_back(entry);
    }
    if (candidates.empty()) {
        return std::string();
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  std::error_code ea, eb;
                  const auto ta = a.last_write_time(ea);
                  const auto tb = b.last_write_time(eb);
                  if (ea || eb) return a.path().string() > b.path().string();
                  return ta > tb;
              });

    const fs::path target = candidates.front().path();
    std::ifstream in(target, std::ios::binary);
    if (!in.is_open()) {
        return std::string();
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff readLen = (size > 32768) ? 32768 : size;
    if (readLen <= 0) {
        return std::string();
    }
    in.seekg(size - readLen, std::ios::beg);
    std::string buffer(static_cast<std::size_t>(readLen), '\0');
    in.read(&buffer[0], readLen);
    const std::streamsize got = in.gcount();
    buffer.resize(static_cast<std::size_t>(got));

    std::regex pattern("session language: ([A-Za-z-]+)");
    std::smatch match;
    std::string last;
    std::string::const_iterator searchStart = buffer.cbegin();
    while (std::regex_search(searchStart, buffer.cend(), match, pattern)) {
        last = match.str(1);
        searchStart = match.suffix().first;
    }
    return last;
}

void writeSessionBanner(const std::string& logDir, const std::string& currentLogPath,
                        const std::string& currentCode) {
    const std::string previous = readPreviousLanguage(logDir, currentLogPath);
    if (!previous.empty() && previous != currentCode) {
        Logger::write("----- UI language changed since last session: " + previous + " -> " + currentCode + " -----",
                      LogLevel::INFO);
    }
    Logger::write("===== session language: " + currentCode + " =====", LogLevel::INFO);
}

}  // namespace LogBanner
}  // namespace i18n
