// tests/ui/framework/ArtifactsListener.h - Catch2 listener: dump window
// screenshot + UIA tree on every failed test case.
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include "framework/Application.h"
#include "framework/Diagnostics.h"

namespace uitest {

class ArtifactsListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseEnded(Catch::TestCaseStats const& stats) override {
        if (!stats.totals.assertions.failed) return;   // only on failure
        // Only capture windows of the GUI instance under test. If the fixture
        // already terminated it, nothing matches and we skip silently.
        const DWORD target = uiTargetPid();
        if (target == 0) return;
        struct Ctx { DWORD pid; HWND hwnd = nullptr; } ctx{ target };
        ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            Ctx* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == c->pid && ::IsWindowVisible(hwnd)) {
                c->hwnd = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hwnd) {
            // Test names are ASCII; widen char-by-char.
            const std::wstring tag(stats.testInfo->name.begin(),
                                   stats.testInfo->name.end());
            saveArtifacts(ctx.hwnd, tag);
        }
    }
};

}
CATCH_REGISTER_LISTENER(uitest::ArtifactsListener)
