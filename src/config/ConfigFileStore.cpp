#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Logger.h"
#include "config/ConfigFileStore.h"

namespace config {

std::string ConfigFileStore::read(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::write("DEBUG: Failed to open file", LogLevel::DEBUG);
        std::string errMsg = "Failed to open configuration file.\n\n";
        errMsg += "Path:\n" + path + "\n\n";
        errMsg += "Check that the file exists and is accessible.";
        throw std::runtime_error(errMsg);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    Logger::write("DEBUG: File read successfully, content length: " + std::to_string(content.length()), LogLevel::DEBUG);
    return content;
}

void ConfigFileStore::write(const std::string& path, const std::string& content) {
    std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::write("Failed to write config to: " + path, LogLevel::ERR);
        throw std::runtime_error("Failed to write configuration file.\n\nPath:\n" + path + "\n\nCheck that the path is writable.");
    }

    file << content;
    file.close();

    Logger::write("Config saved to: " + path, LogLevel::INFO);
}

} // namespace config
