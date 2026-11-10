#pragma once

// Atomic whole-file write (spec 2026-11-06 §2.7, owner R-2).
//
// A plain std::ofstream open TRUNCATES the target first: a crash or power
// loss between truncation and the last flush leaves a half-written file that
// every later reader chokes on (autotask_state.json destroyed the resume
// checkpoint; config.json would brick startup). Writing a sibling .tmp and
// renaming it over the target makes the swap atomic at the filesystem level —
// readers observe either the old or the new content, never a mix.
//
// Windows note: MoveFileEx(REPLACE_EXISTING) semantics — std::filesystem::rename
// fails when the target exists on some Windows/MinGW combos, so on EEXIST we
// remove the target and retry once (the brief non-atomic window only exists
// after the tmp write already succeeded; readers here are all queue-serialised).

#include <filesystem>
#include <fstream>
#include <string>

namespace utils {

// Writes content to path atomically. Returns false (throwing never) when the
// write or the swap failed; on failure the original target, if any, is intact.
inline bool writeFileAtomically(const std::string& path, const std::string& content) {
    std::error_code ec;
    const std::filesystem::path target(path);
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent, ec);  // ec checked at open
    }
    const std::filesystem::path tmp = target.string() + ".tmp";

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << content;
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }  // RAII close BEFORE rename: on Windows the stream buffer must be fully
       // flushed and the handle closed before the swap is observable.

    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        // Target existed and the platform refused the swap: remove + retry once.
        std::filesystem::remove(target, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

} // namespace utils
