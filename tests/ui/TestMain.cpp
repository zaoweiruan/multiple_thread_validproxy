// tests/ui/TestMain.cpp — Catch2 entry for UI automation tests.
#include <catch2/catch_session.hpp>
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include "framework/ArtifactsListener.h"   // registers failure-artifact listener
#include "framework/UIAutomation.h"

int main(int argc, char* argv[]) {
    // COM apartment for UIA; balanced at process exit.
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    uitest::Uia::instance().init();   // create IUIAutomation + desktop element

    // Ensure the artifacts root exists before any reporter tries to open
    // test-results/results.xml (JUnit reporter aborts the run if it cannot).
    try {
        std::filesystem::create_directories(L"test-results");
    } catch (...) {
        // Non-fatal: individual diagnostics will report their own failures.
    }

    // Default reporters: console on stdout + JUnit XML for CI, unless caller overrides.
    bool hasReporter = false;
    std::vector<std::string> args(argv, argv + argc);
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-r" || args[i] == "--reporter") { hasReporter = true; break; }
    }
    std::vector<char*> finalArgs(argv, argv + argc);
    std::string junitOpt = "-r junit::out=test-results/results.xml";
    if (!hasReporter) {
        finalArgs.push_back(const_cast<char*>("-r"));
        finalArgs.push_back(const_cast<char*>("console"));
        finalArgs.push_back(const_cast<char*>(junitOpt.c_str()));
    }

    int rc = Catch::Session().run(static_cast<int>(finalArgs.size()), finalArgs.data());
    // Release UIA pointers BEFORE the COM apartment goes away; static-singleton
    // destruction would otherwise run after CoUninitialize and crash at exit.
    uitest::Uia::instance().shutdown();
    ::CoUninitialize();
    return rc;
}
