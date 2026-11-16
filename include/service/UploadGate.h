// include/service/UploadGate.h - gate for the "upload to cloud" capability.
//
// Uploading can overwrite the remote config/subscriptions, so the feature is
// locked behind an environment token:
//
//   VALIDPROXY_UPLOAD_TOKEN must exist AND equal the value baked in at build time
//
// Anything else (absent, empty, different) keeps the entry point disabled.
//
// The expected token is injected by CMake from the gitignored
// resources/upload_token.inc into build/include/UploadGateToken.h. When that
// file is absent the expected token is empty, which keeps upload permanently
// disabled - the safe default.
//
// This is a capability switch, not a security boundary: the expected value is
// recoverable from the binary, exactly like the WebDAV credentials. It exists to
// stop accidental uploads, not to stop a determined reverse engineer.
#ifndef SERVICE_UPLOAD_GATE_H
#define SERVICE_UPLOAD_GATE_H

namespace service {

// The token compiled into this binary (empty string when not configured).
const char* expectedUploadToken();

// Pure predicate: exact, case-sensitive comparison. A null or empty value on
// either side never matches, so an unconfigured build can never open the gate.
bool tokenEquals(const char* expected, const char* provided);

// Compares `providedValue` against the compiled-in token.
bool uploadTokenMatches(const char* providedValue);

// Reads VALIDPROXY_UPLOAD_TOKEN from the process environment and applies
// uploadTokenMatches(). Called at startup to decide whether the menu item is
// enabled, and again before acting.
bool isUploadEnabled();

} // namespace service

#endif // SERVICE_UPLOAD_GATE_H
