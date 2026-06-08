// src/main_gui.cpp — Pure GUI entry point (WIN32 subsystem)
// No CLI command handling. No console output (stdout/stderr are discarded by WIN32 subsystem).

#include "UIApp.h"
#include "Utils.h"
#include "Logger.h"
#include "ConfigReader.h"
#include "XrayManager.h"

#include <sqlite3.h>
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string exeDir = utils::getExecutableDir();
    std::string configPath = (std::filesystem::path(exeDir) / "config.json").string();

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

    // Initialize Logger FIRST (before load / DB open), so ConfigReader::load()
    // can log diagnostics and any early failure is visible in the log file.
    Logger::init(logDir.string(), "ui");
    Logger::write("gui entry: Logger::init completed", LogLevel::INFO);
    Logger::setConsoleEnabled(false);

    // Load configuration
    std::optional<config::AppConfig> appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::string errMsg = "Failed to load configuration file.\n\n";
        errMsg += "Config path:\n" + configPath;
        MessageBoxA(NULL, errMsg.c_str(), "Configuration Error", MB_ICONERROR | MB_OK);
        Logger::close();
        return 1;
    }

    // Apply config-specified log levels
    Logger::setFileEnabled(appConfig->log_enabled);
    Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));

    // Open database
    sqlite3* db = nullptr;
    if (appConfig->database_path.empty()) {
        MessageBoxA(NULL,
                    "Database path is empty in configuration file.\nPlease check the database.path setting and restart.",
                    "Configuration Error",
                    MB_ICONERROR | MB_OK);
        return 1;
    }
    if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
        std::string errMsg = "Failed to open database.\n\n";
        errMsg += "Error: " + std::string(sqlite3_errmsg(db)) + "\n\n";
        errMsg += "Database path from config:\n" + appConfig->database_path;
        MessageBoxA(NULL, errMsg.c_str(), "Database Error", MB_ICONERROR | MB_OK);
        Logger::close();
        return 1;
    }

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
