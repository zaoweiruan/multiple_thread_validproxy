// src/main_gui.cpp — Pure GUI entry point (WIN32 subsystem)
// No CLI command handling. No console output (stdout/stderr are discarded by WIN32 subsystem).

#include "UIApp.h"
#include "Utils.h"
#include "Logger.h"
#include "ConfigReader.h"
#include "XrayManager.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string configPath = "config.json";
    std::string exeDir = utils::getExecutableDir();

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-c" || arg == "--config") {
            if (++i < argc) {
                std::filesystem::path fp(argv[i]);
                if (fp.is_relative()) {
                    fp = std::filesystem::path(exeDir) / fp;
                }
                configPath = fp.lexically_normal().string();
            }
        }
    }

    // Create log directory
    std::filesystem::path logDir = std::filesystem::path(exeDir) / "log";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }

    // Load configuration
    std::optional<config::AppConfig> appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::cerr << "Failed to load config from: " << configPath << std::endl;
        return 1;
    }

    // Open database
    sqlite3* db = nullptr;
    if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    // Initialize Logger for UI mode
    Logger::init(logDir.string(), "ui");
    Logger::setFileEnabled(appConfig->log_enabled);
    Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
    Logger::setConsoleEnabled(false);

    // Strip non-wxWidgets flags from argv before passing to wxEntry
    // (-ui, --ui, --gui are CLI-legacy flags from the old hybrid binary)
    std::vector<char*> wxArgv;
    wxArgv.reserve(argc);
    wxArgv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a != "-ui" && a != "--ui" && a != "--gui") {
            wxArgv.push_back(argv[i]);
        }
    }
    int wxArgc = static_cast<int>(wxArgv.size());

    // Enter wxWidgets event loop (blocks until GUI exits)
    wxApp::SetInstance(new UIApp(*appConfig, db));
    int ret = wxEntry(wxArgc, wxArgv.data());

    // Cleanup after GUI exits
    XrayManager::release();
    sqlite3_close(db);
    Logger::close();

    return ret;
}
