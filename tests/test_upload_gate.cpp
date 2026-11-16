// tests/test_upload_gate.cpp
//
// Unit tests for the upload capability gate. No network, no database: the gate is
// a pure string comparison plus one environment lookup.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "service/UploadGate.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Saves VALIDPROXY_UPLOAD_TOKEN and restores it when the test ends, so ordering
// between cases can never leak state. On Windows this goes through the Win32
// environment block - exactly what service::isUploadEnabled() reads - instead of
// the CRT's _putenv_s, which crashes on this MinGW runtime when clearing a value.
class UploadTokenEnvGuard {
public:
    UploadTokenEnvGuard() : hadValue_(readRaw(original_)) {}

    ~UploadTokenEnvGuard() {
        if (hadValue_) {
            setRaw(original_);
        } else {
            clearRaw();
        }
    }

    static bool readRaw(std::string& out) {
#ifdef _WIN32
        char buffer[512] = {0};
        const DWORD len = GetEnvironmentVariableA("VALIDPROXY_UPLOAD_TOKEN", buffer,
                                                  static_cast<DWORD>(sizeof(buffer)));
        if (len == 0 || len >= sizeof(buffer)) {
            out.clear();
            return false;
        }
        out.assign(buffer, len);
        return true;
#else
        const char* value = std::getenv("VALIDPROXY_UPLOAD_TOKEN");
        if (value == nullptr) {
            out.clear();
            return false;
        }
        out.assign(value);
        return true;
#endif
    }

    static void setRaw(const std::string& value) {
#ifdef _WIN32
        SetEnvironmentVariableA("VALIDPROXY_UPLOAD_TOKEN", value.c_str());
#else
        setenv("VALIDPROXY_UPLOAD_TOKEN", value.c_str(), 1);
#endif
    }

    static void clearRaw() {
#ifdef _WIN32
        SetEnvironmentVariableA("VALIDPROXY_UPLOAD_TOKEN", nullptr);
#else
        unsetenv("VALIDPROXY_UPLOAD_TOKEN");
#endif
    }

private:
    // Declaration order matters: readRaw() writes into original_, so it must be
    // constructed before hadValue_'s initializer runs.
    std::string original_;
    bool hadValue_;
};

} // namespace

TEST(UploadGateTest, TokenEqualsRejectsEmptyExpected) {
    EXPECT_FALSE(service::tokenEquals("", "anything"));
    EXPECT_FALSE(service::tokenEquals("", ""));
}

TEST(UploadGateTest, TokenEqualsRejectsNullExpected) {
    EXPECT_FALSE(service::tokenEquals(nullptr, "anything"));
}

TEST(UploadGateTest, TokenEqualsRejectsEmptyProvided) {
    EXPECT_FALSE(service::tokenEquals("secret", ""));
}

TEST(UploadGateTest, TokenEqualsRejectsNullProvided) {
    EXPECT_FALSE(service::tokenEquals("secret", nullptr));
}

TEST(UploadGateTest, TokenEqualsAcceptsExactMatch) {
    EXPECT_TRUE(service::tokenEquals("secret-token", "secret-token"));
}

TEST(UploadGateTest, TokenEqualsRejectsPrefixMatch) {
    EXPECT_FALSE(service::tokenEquals("secret-token", "secret"));
    EXPECT_FALSE(service::tokenEquals("secret", "secret-token"));
}

TEST(UploadGateTest, TokenEqualsRejectsCaseDifference) {
    EXPECT_FALSE(service::tokenEquals("Secret-Token", "secret-token"));
}

TEST(UploadGateTest, TokenEqualsRejectsTrailingWhitespace) {
    EXPECT_FALSE(service::tokenEquals("secret-token", "secret-token "));
    EXPECT_FALSE(service::tokenEquals("secret-token ", "secret-token"));
}

TEST(UploadGateTest, ExpectedTokenIsNeverNull) {
    // An unprovisioned build compiles an empty literal, never a null pointer.
    ASSERT_NE(service::expectedUploadToken(), nullptr);
}

TEST(UploadGateTest, UploadTokenMatchesUsesCompiledToken) {
    const std::string expected = service::expectedUploadToken();
    if (expected.empty()) {
        // Unprovisioned build: nothing may ever match.
        EXPECT_FALSE(service::uploadTokenMatches(""));
        EXPECT_FALSE(service::uploadTokenMatches("anything"));
    } else {
        EXPECT_TRUE(service::uploadTokenMatches(expected.c_str()));
        EXPECT_FALSE(service::uploadTokenMatches((expected + "x").c_str()));
    }
}

TEST(UploadGateTest, IsUploadEnabledIsFalseWhenEnvVarIsAbsent) {
    UploadTokenEnvGuard guard;
    UploadTokenEnvGuard::clearRaw();
    EXPECT_FALSE(service::isUploadEnabled());
}

TEST(UploadGateTest, IsUploadEnabledIsFalseWhenEnvVarIsEmpty) {
    UploadTokenEnvGuard guard;
    UploadTokenEnvGuard::setRaw("");
    EXPECT_FALSE(service::isUploadEnabled());
}

TEST(UploadGateTest, IsUploadEnabledIsFalseWhenEnvVarDiffers) {
    UploadTokenEnvGuard guard;
    UploadTokenEnvGuard::setRaw("definitely-not-the-token");
    EXPECT_FALSE(service::isUploadEnabled());
}

TEST(UploadGateTest, IsUploadEnabledTracksTheCompiledToken) {
    UploadTokenEnvGuard guard;
    const std::string expected = service::expectedUploadToken();
    if (expected.empty()) {
        // Unprovisioned build: setting any value must not open the gate.
        UploadTokenEnvGuard::setRaw("anything");
        EXPECT_FALSE(service::isUploadEnabled());
        GTEST_SKIP() << "upload token not provisioned in this build";
    }
    UploadTokenEnvGuard::setRaw(expected);
    EXPECT_TRUE(service::isUploadEnabled());

    // Case change must not pass.
    std::string flipped = expected;
    for (char& c : flipped) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
            break;
        }
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
            break;
        }
    }
    if (flipped != expected) {
        UploadTokenEnvGuard::setRaw(flipped);
        EXPECT_FALSE(service::isUploadEnabled());
    }
}
