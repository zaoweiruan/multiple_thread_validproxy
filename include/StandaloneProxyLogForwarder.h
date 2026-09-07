#ifndef STANDALONE_PROXY_LOG_FORWARDER_H
#define STANDALONE_PROXY_LOG_FORWARDER_H

#include <string>
#include <functional>
#include <windows.h>
#include "Logger.h"  // LogLevel

namespace standalone_proxy {

// Forwards a backing process's stdout/stderr (read from `readEnd`, an anonymous
// pipe read handle) into `logFn`, splitting the byte stream into lines and
// routing error-like lines to LogLevel::ERR. Closes `readEnd` once EOF is
// reached (i.e. when the child closes its end of the pipe).
//
// `indexId` is embedded in every emitted message as the "[xray:<id>] " prefix,
// matching the format the user watches in the local log. `logFn` has the shape
// `(message, level) -> void`; pass a Logger::write-shaped lambda for production
// use, or a capturing lambda in tests.
void forwardChildLog(HANDLE readEnd, const std::string& indexId,
                    std::function<void(const std::string&, LogLevel)> logFn);

}  // namespace standalone_proxy

#endif  // STANDALONE_PROXY_LOG_FORWARDER_H
