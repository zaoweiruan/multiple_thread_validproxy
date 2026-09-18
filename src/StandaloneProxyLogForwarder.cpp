#include "StandaloneProxyLogForwarder.h"

#include <cctype>
#include <string>

namespace standalone_proxy {

namespace {

bool isErrorLine(const std::string& line) {
    std::string s = line;
    for (char& c : s) {
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return s.find("error") != std::string::npos
        || s.find("panic") != std::string::npos
        || s.find("fatal") != std::string::npos
        || s.find("fail") != std::string::npos
        || s.find("invalid") != std::string::npos
        || s.find("cannot") != std::string::npos
        || s.find("unable") != std::string::npos
        || s.find("exception") != std::string::npos
        || s.find("refused") != std::string::npos
        || s.find("denied") != std::string::npos;
}

}  // namespace

void forwardChildLog(HANDLE readEnd, const std::string& indexId,
                    std::function<void(const std::string&, LogLevel)> logFn) {
    if (readEnd == nullptr || readEnd == INVALID_HANDLE_VALUE) {
        return;
    }
    char buf[1024];
    DWORD n = 0;
    std::string line;
    while (ReadFile(readEnd, buf, sizeof(buf), &n, nullptr) && n > 0) {
        line.append(buf, static_cast<size_t>(n));
        size_t pos = 0;
        while ((pos = line.find('\n')) != std::string::npos) {
            std::string l = line.substr(0, pos);
            if (!l.empty() && l.back() == '\r') {
                l.pop_back();
            }
            logFn("[xray:" + indexId + "] " + l,
                  isErrorLine(l) ? LogLevel::ERR : LogLevel::TRACE);
            line.erase(0, pos + 1);
        }
    }
    if (!line.empty()) {
        if (line.back() == '\r') {
            line.pop_back();
        }
        logFn("[xray:" + indexId + "] " + line,
              isErrorLine(line) ? LogLevel::ERR : LogLevel::TRACE);
    }
    CloseHandle(readEnd);
}

}  // namespace standalone_proxy
