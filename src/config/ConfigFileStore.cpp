#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Logger.h"
#include "config/ConfigFileStore.h"
#include "Utils.h"
#include "AtomicFileWrite.h"

namespace config {

std::string ConfigFileStore::read(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::string errMsg = "Failed to open configuration file.\n\n";
        errMsg += "Path:\n" + utils::toWindowsPath(path) + "\n\n";
        errMsg += "Check that the file exists and is accessible.";
        throw std::runtime_error(errMsg);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    return content;
}

void ConfigFileStore::write(const std::string& path, const std::string& content) {
    // Atomic swap (.tmp + rename): config.json is rewritten while the app
    // runs, and a truncating half-file after a crash would brick every
    // later startup read (spec 2026-11-06 §2.7).
    if (!utils::writeFileAtomically(path, content)) {
        Logger::write("Failed to write config to: " + utils::toWindowsPath(path), LogLevel::ERR);
        throw std::runtime_error("Failed to write configuration file.\n\nPath:\n" + utils::toWindowsPath(path) + "\n\nCheck that the path is writable.");
    }

    Logger::write("Config saved to: " + utils::toWindowsPath(path), LogLevel::INFO);
}

} // namespace config
