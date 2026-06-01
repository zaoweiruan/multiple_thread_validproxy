// src/main_gui.cpp — Pure GUI entry point (WIN32 subsystem)
// No CLI command handling. No console output (stdout/stderr are discarded by WIN32 subsystem).

#include "UIApp.h"
#include <wx/app.h>
#include <wx/msgdlg.h>

#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>

int main(int argc, char* argv[]) {
    // Only parse -c/--config for config path
    std::string configPath = "config.json";
    std::string exeDir;
    {
        std::filesystem::path p(argv[0]);
        exeDir = p.parent_path().lexically_normal().string();
    }

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
        // -ui, --ui, --gui: silently accepted for backward compatibility
        // -h, --help: silently ignored (no console to output to)
        // All other unknown args: silently ignored
    }

    wxApp::SetInstance(new UIApp());
    return wxEntry(argc, argv);
}
