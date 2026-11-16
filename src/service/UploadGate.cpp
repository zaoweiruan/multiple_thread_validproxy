// src/service/UploadGate.cpp
#include "service/UploadGate.h"

#include <cstdlib>
#include <cstring>

#include "UploadGateToken.h"  // generated: VP_EXPECTED_UPLOAD_TOKEN

#ifdef _WIN32
#include <windows.h>
#endif

namespace service {

const char* expectedUploadToken() {
    return VP_EXPECTED_UPLOAD_TOKEN;
}

bool tokenEquals(const char* expected, const char* provided) {
    // An empty expected token means "not configured": never open the gate.
    if (expected == nullptr || expected[0] == '\0') {
        return false;
    }
    if (provided == nullptr || provided[0] == '\0') {
        return false;
    }
    // Exact, case-sensitive comparison; no trimming, no prefix match.
    return std::strcmp(provided, expected) == 0;
}

bool uploadTokenMatches(const char* providedValue) {
    return tokenEquals(expectedUploadToken(), providedValue);
}

bool isUploadEnabled() {
#ifdef _WIN32
    // GetEnvironmentVariableA reports the required size when the buffer is too
    // small, so an oversized token is rejected instead of being truncated into a
    // spurious match.
    char buffer[256] = {0};
    const DWORD len = GetEnvironmentVariableA("VALIDPROXY_UPLOAD_TOKEN", buffer,
                                              static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) {
        return false;
    }
    return uploadTokenMatches(buffer);
#else
    return uploadTokenMatches(std::getenv("VALIDPROXY_UPLOAD_TOKEN"));
#endif
}

} // namespace service
