// ---------------------------------------------------------------------------
// test_ui_string_encoding.cpp
//
// Regression guard for the Win7 bug (2026-11-17):
//   src/ui/** must not contain narrow (non-L) string literals with non-ASCII
//   bytes. wxString's implicit const char* constructor converts through
//   wxConvLibc (Windows ANSI code page), so UTF-8 source literals are decoded
//   as GBK/CP1252. Even-length sequences turn into mojibake ("更新" -> "鏇存柊"),
//   odd-length ones fail to convert and yield an EMPTY wxString, which trips
//   wx's "A non-stock menu item with an empty label?" assert (and the follow-up
//   "invalid stock item ID" assert) inside wxMenu::Append().
//
// Narrow non-ASCII literals are still legitimate for log/JSON/DB strings; this
// test therefore only scans the UI layer, where every user-visible string must
// be a wide literal L"...", or an explicit wxString::FromUTF8(u8"...") / .c_str()
// conversion of runtime data.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Violation {
    std::string file;
    int line = 0;
    std::string literal;
};

bool isNonAsciiLiteralBody(const std::string& body) {
    for (std::size_t i = 0; i < body.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(body[i]);
        if (ch > 127) {
            return true;
        }
    }
    return false;
}

// Opaque wrappers that already produce a wide literal: wxT("中文"), _T("中文"),
// wxS("中文"). The literal itself stays narrow in the source but is converted at
// compile time, so it must not be flagged.
bool isWideWrapper(const std::string& code) {
    static const char* kMacros[] = { "wxT(", "_T(", "wxS(" };
    std::string trimmed = code;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    for (const char* macro : kMacros) {
        const std::string m(macro);
        if (trimmed.size() >= m.size()
            && trimmed.compare(trimmed.size() - m.size(), m.size(), m) == 0) {
            return true;
        }
    }
    return false;
}

// Minimal C++ lexer: walks the source and reports narrow non-ASCII literals
// outside comments. Raw strings and char literals are skipped.
std::vector<Violation> scanSource(const std::string& path, const std::string& src) {
    std::vector<Violation> out;
    const std::size_t n = src.size();
    std::size_t i = 0;
    int line = 1;

    std::string codeSoFar;

    while (i < n) {
        char ch = src[i];

        if (ch == '\n') {
            ++line;
            codeSoFar.push_back(ch);
            ++i;
            continue;
        }

        // line comment
        if (ch == '/' && i + 1 < n && src[i + 1] == '/') {
            std::size_t j = src.find('\n', i);
            if (j == std::string::npos) {
                j = n;
            }
            i = j;
            continue;
        }

        // block comment
        if (ch == '/' && i + 1 < n && src[i + 1] == '*') {
            std::size_t j = src.find("*/", i + 2);
            if (j == std::string::npos) {
                j = n;
            } else {
                j += 2;
            }
            for (std::size_t k = i; k < j; ++k) {
                if (src[k] == '\n') {
                    ++line;
                }
            }
            i = j;
            continue;
        }

        // character literal (with optional u8/L/u/U prefix)
        std::size_t charPrefix = 0;
        if (i + 2 < n && src.compare(i, 2, "u8") == 0 && src[i + 2] == '\'') {
            charPrefix = 2;
        } else if (i + 1 < n && (src[i] == 'L' || src[i] == 'u' || src[i] == 'U')
                   && src[i + 1] == '\'') {
            charPrefix = 1;
        }
        if (charPrefix > 0 || ch == '\'') {
            std::size_t j = i + charPrefix + 1;
            while (j < n) {
                if (src[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (src[j] == '\'') {
                    ++j;
                    break;
                }
                if (src[j] == '\n') {
                    ++line;
                }
                ++j;
            }
            i = j;
            continue;
        }

        // raw string literal: R"delim( ... )delim"
        std::size_t rawPrefix = 0;
        if (i + 1 < n && src[i] == 'R' && src[i + 1] == '"') {
            rawPrefix = 0;
        } else if (i + 2 < n && (src.compare(i, 2, "u8") == 0 || src.compare(i, 2, "LR") == 0
                                 || src.compare(i, 2, "uR") == 0 || src.compare(i, 2, "UR") == 0)
                   && src[i + 2] == '"') {
            rawPrefix = 1;
        }
        if (rawPrefix > 0 || (ch == 'R' && i + 1 < n && src[i + 1] == '"')) {
            std::size_t delimStart = i + rawPrefix + 2;
            std::size_t paren = src.find('(', delimStart);
            if (paren == std::string::npos) {
                break;
            }
            const std::string delim = src.substr(delimStart, paren - delimStart);
            const std::string terminator = ")" + delim + "\"";
            std::size_t end = src.find(terminator, paren);
            if (end == std::string::npos) {
                end = n;
            } else {
                end += terminator.size();
            }
            for (std::size_t k = i; k < end; ++k) {
                if (src[k] == '\n') {
                    ++line;
                }
            }
            i = end;
            continue;
        }

        // string literal
        std::size_t prefixLen = 0;
        if (i + 2 < n && src.compare(i, 2, "u8") == 0 && src[i + 2] == '"') {
            prefixLen = 2;
        } else if (i + 1 < n && (src[i] == 'L' || src[i] == 'u' || src[i] == 'U')
                   && src[i + 1] == '"') {
            prefixLen = 1;
        }
        if (prefixLen > 0 || ch == '"') {
            const std::string prefix = src.substr(i, prefixLen);
            std::size_t j = i + prefixLen + 1;
            const std::size_t bodyStart = j;
            while (j < n) {
                if (src[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (src[j] == '"') {
                    break;
                }
                if (src[j] == '\n') {
                    ++line;
                }
                ++j;
            }
            const std::string body = src.substr(bodyStart, (j < n ? j : n) - bodyStart);

            bool violation = false;
            if (prefix == "L" || prefix == "u" || prefix == "U") {
                // Wide literal: correct for wxString parameters.
                violation = false;
            } else if (!isNonAsciiLiteralBody(body)) {
                violation = false;
            } else if (prefix == "u8") {
                // Explicit UTF-8 bytes: correct for std::string / Logger / JSON
                // payloads. UI sinks convert these with wxString::FromUTF8.
                violation = false;
            } else if (isWideWrapper(codeSoFar)) {
                // wxT("...")/_T("...")/wxS("..."): already a wide literal.
                violation = false;
            } else {
                violation = true;
            }

            if (violation) {
                Violation v;
                v.file = path;
                v.line = line;
                v.literal = prefix + "\"" + body + "\"";
                out.push_back(v);
            }

            if (j < n) {
                i = j + 1;
            } else {
                i = n;
            }
            continue;
        }

        codeSoFar.push_back(ch);
        ++i;
    }

    return out;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data;
}

class UiStringScanTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const std::filesystem::path root =
            std::filesystem::path(VALIDPROXY_SOURCE_DIR) / "src" / "ui";
        ASSERT_TRUE(std::filesystem::exists(root)) << "missing UI source dir: " << root.string();

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path& file = entry.path();
            const std::string ext = file.extension().string();
            if (ext != ".cpp" && ext != ".h") {
                continue;
            }
            const std::string text = readFile(file);
            if (text.empty()) {
                continue;
            }
            std::vector<Violation> found = scanSource(file.filename().string(), text);
            violations_.insert(violations_.end(), found.begin(), found.end());
        }
    }

    static std::vector<Violation> violations_;
};

std::vector<Violation> UiStringScanTest::violations_;

} // namespace

// The scanner itself must work: guard against a silent no-op implementation.
TEST(UiStringScannerSelfCheck, DetectsNarrowNonAsciiLiteral) {
    const std::string sample =
        "#include <wx/wx.h>\n"
        "void f() { menu->Append(1, \"加入代理池\"); }\n"
        "void g() { ok(L\"宽字符中文\"); }\n"
        "// \"注释里的中文字面量\" 不算\n"
        "void h() { wxString s = wxString::FromUTF8(u8\"来自数据库\"); }\n";

    std::vector<Violation> found = scanSource("sample.cpp", sample);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].line, 2);
}

TEST(UiStringScannerSelfCheck, AcceptsWideAndExplicitUtf8Literals) {
    const std::string sample =
        "void f() { list->InsertColumn(0, u8\"任务ID\"); }\n"
        "void g() { label->SetLabel(L\"任务ID\"); }\n"
        "void h() { std::string s = u8\"已取消\"; }\n"
        "void i() { b->SetLabel(wxT(\"名称\")); }\n"
        "char c = '\xd6';\n";

    std::vector<Violation> found = scanSource("sample.cpp", sample);
    EXPECT_TRUE(found.empty());
}

// Win7 regression: no narrow non-ASCII literals in the UI layer.
TEST_F(UiStringScanTest, NoNarrowNonAsciiLiteralsInUiLayer) {
    std::string report;
    for (const Violation& v : violations_) {
        report += v.file + ":" + std::to_string(v.line) + ": " + v.literal + "\n";
    }
    EXPECT_TRUE(violations_.empty())
        << "Narrow (non-L) non-ASCII string literals in src/ui make wxString decode\n"
        << "UTF-8 source bytes with the Windows ANSI code page: even-length text\n"
        << "becomes mojibake, odd-length text becomes an EMPTY string (wxMenu empty\n"
        << "label assert). Use L\"...\" or wxString::FromUTF8(u8\"...\").\n"
        << report;
}
