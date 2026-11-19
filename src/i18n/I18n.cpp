#include "i18n/I18n.h"

#include <atomic>
#include <cassert>
#include <cstddef>

#include "i18n/StringTable.h"
#include "Logger.h"

namespace i18n {
namespace {

std::atomic<int> g_lang(static_cast<int>(Language::ZhCN));
std::atomic<bool> g_missingWarned(false);

const wchar_t* kMissingW = L"(missing translation)";
const char* kMissingU8 = "(missing translation)";

bool isInRange(StrId id) {
    int index = static_cast<int>(id);
    return index >= 0 && index < table::count();
}

void reportMissing(const char* which, int index) {
    assert(false && "i18n: StrId out of range");
    bool expected = false;
    if (g_missingWarned.compare_exchange_strong(expected, true)) {
        Logger::write(std::string("[i18n] invalid StrId index: ") + std::to_string(index)
                          + " (" + which + "), returning placeholder",
                      LogLevel::WARN);
    }
}

const wchar_t* const* wideTable() {
    return g_lang.load(std::memory_order_relaxed) == static_cast<int>(Language::EnUS)
               ? table::enW()
               : table::zhW();
}

const char* const* narrowTable() {
    return g_lang.load(std::memory_order_relaxed) == static_cast<int>(Language::EnUS)
               ? table::enU8()
               : table::zhU8();
}

}  // namespace

void setLanguage(Language lang) {
    g_lang.store(static_cast<int>(lang), std::memory_order_relaxed);
}

Language language() {
    return g_lang.load(std::memory_order_relaxed) == static_cast<int>(Language::EnUS)
               ? Language::EnUS
               : Language::ZhCN;
}

Language resolveStartupLanguage(const std::string& configured) {
    Language parsed = Language::ZhCN;
    if (!parseLanguage(configured, parsed)) {
        return Language::ZhCN;
    }
    return parsed;
}

const wchar_t* w(StrId id) {
    if (!isInRange(id)) {
        reportMissing("w", static_cast<int>(id));
        return kMissingW;
    }
    const wchar_t* value = wideTable()[static_cast<int>(id)];
    return value != nullptr ? value : kMissingW;
}

const char* u8(StrId id) {
    if (!isInRange(id)) {
        reportMissing("u8", static_cast<int>(id));
        return kMissingU8;
    }
    const char* value = narrowTable()[static_cast<int>(id)];
    return value != nullptr ? value : kMissingU8;
}

std::string u8f(StrId id, std::initializer_list<std::string> args) {
    const std::string pattern(u8(id));
    std::string out;
    out.reserve(pattern.size() + 16);

    std::initializer_list<std::string>::const_iterator next = args.begin();
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        std::size_t hole = pattern.find("{}", pos);
        if (hole == std::string::npos) {
            out.append(pattern, pos, std::string::npos);
            break;
        }
        out.append(pattern, pos, hole - pos);
        if (next == args.end()) {
            out.append("{}");
        } else {
            out.append(*next);
            ++next;
        }
        pos = hole + 2;
    }
    return out;
}

std::wstring wf(StrId id, std::initializer_list<std::wstring> args) {
    const std::wstring pattern(w(id));
    std::wstring out;
    out.reserve(pattern.size() + 16);

    std::initializer_list<std::wstring>::const_iterator next = args.begin();
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        std::size_t hole = pattern.find(L"{}", pos);
        if (hole == std::wstring::npos) {
            out.append(pattern, pos, std::wstring::npos);
            break;
        }
        out.append(pattern, pos, hole - pos);
        if (next == args.end()) {
            out.append(L"{}");
        } else {
            out.append(*next);
            ++next;
        }
        pos = hole + 2;
    }
    return out;
}

}  // namespace i18n
