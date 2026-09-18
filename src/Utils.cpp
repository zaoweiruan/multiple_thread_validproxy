#include <winsock2.h>
#include <ws2tcpip.h>
#include "Utils.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <set>
    #include <cctype>
    #include <vector>
    #include <iphlpapi.h>
    #include "Logger.h"

namespace utils {
    std::string getCurrentTimestamp() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return std::to_string(timestamp);
    }

    std::string getCurrentTimestampFormatted() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf);
    }
}

namespace utils {
    std::string getExecutableDir() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exePath = buffer;
        size_t pos = exePath.rfind('\\');
        if (pos != std::string::npos) {
            exePath = exePath.substr(0, pos);
        }
        return exePath;
    }

    std::string generateUniqueId() {
        static std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        static std::uniform_int_distribution<int> firstDist(0, 1);
        static std::uniform_int_distribution<long long> restDist(0, 999999999999999999);
        
        int first = 4 + firstDist(rng);
        long long rest = restDist(rng);
        
        std::ostringstream oss;
        oss << first << std::setw(18) << std::setfill('0') << rest;
        return oss.str();
    }

    std::string joinUrl(const std::string& base, const std::string& suffix) {
        if (base.empty()) return suffix;
        if (suffix.empty()) return base;

        std::string b = base;
        std::string s = suffix;

        while (!b.empty() && b.back() == '/') {
            b.pop_back();
        }
        while (!s.empty() && s.front() == '/') {
            s.erase(s.begin());
        }

        if (b.empty()) return s;
        if (s.empty()) return b;

        return b + "/" + s;
    }

    std::string getProtocolName(const std::string& configType) {
        if (configType == "1") return "VMess";
        if (configType == "2") return "Custom";
        if (configType == "3") return "Shadowsocks";
        if (configType == "4") return "SOCKS";
        if (configType == "5") return "VLESS";
        if (configType == "6") return "Trojan";
        if (configType == "7") return "Hysteria2";
        if (configType == "8") return "TUIC";
        if (configType == "9") return "WireGuard";
        if (configType == "10") return "HTTP";
        if (configType == "11") return "Anytls";
        if (configType == "12") return "Naive";
        if (configType == "16") return "WireGuard";
        if (configType == "17") return "TUIC";
        return "Unknown(" + configType + ")";
    }
    
void sendNotification(const std::string& title, const std::string& message) {
         static bool initialized = false;
         static UINT uid = 1;

         NOTIFYICONDATAW nid = {0};
         nid.cbSize = sizeof(NOTIFYICONDATAW);
         nid.uID = uid;
         nid.hWnd = GetConsoleWindow();

         if (!initialized) {
             nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
             nid.hIcon = (HICON)(LONG_PTR)LoadIconW(NULL, MAKEINTRESOURCEW(32512));
             nid.uCallbackMessage = WM_USER;
             MultiByteToWideChar(CP_UTF8, 0, "validproxy", -1, nid.szTip, 128);
             Shell_NotifyIconW(NIM_ADD, &nid);

             atexit([]() {
                 NOTIFYICONDATAW nid = {0};
                 nid.cbSize = sizeof(NOTIFYICONDATAW);
                 nid.uID = 1;
                 nid.hWnd = GetConsoleWindow();
                 nid.uFlags = 0;
                 Shell_NotifyIconW(NIM_DELETE, &nid);
             });

             initialized = true;
         }

         nid.uFlags = NIF_INFO;
         nid.dwInfoFlags = NIIF_INFO;
         MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nid.szInfoTitle, 64);
         MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nid.szInfo, 256);

         Shell_NotifyIconW(NIM_MODIFY, &nid);
     }

bool isValidUrlFormat(const std::string& url) {
         if (url.find("http://") != 0 && url.find("https://") != 0) {
             return false;
         }
         size_t schemeEnd = url.find("://");
         if (schemeEnd == std::string::npos) return false;
         std::string hostPart = url.substr(schemeEnd + 3);
         size_t pathStart = hostPart.find('/');
         std::string domain = (pathStart != std::string::npos)
                             ? hostPart.substr(0, pathStart)
                             : hostPart;
         return domain.find('.') != std::string::npos && domain != ".";
     }

     bool isValidNetwork(const std::string& network) {
         if (network.empty()) return false;

         std::string lower = network;
         std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

         static const std::set<std::string> valid = {
             "tcp","ws","grpc","h2","httpupgrade","kcp","xhttp","http","quic"
         };

         if (valid.count(lower) == 0) {
             if (lower == "raw" || lower == "tcp,udp") {
                 return true;
             }
         }
        return valid.count(lower) > 0;
    }

    // Detect whether `port` is held by any socket (our process or another) by
    // enumerating the system TCP endpoint table — the same source `netstat` uses.
    // A fresh listen() on `port` fails if an existing endpoint occupies it in a
    // state that blocks binding. We treat LISTEN and TIME_WAIT as blocking:
    //   * LISTEN    -> a real server (e.g. xray SOCKS5 on 0.0.0.0 / [::]) holds it
    //                  (the 2026-08-31 wildcard false-free regression).
    //   * TIME_WAIT -> a just-closed socket (e.g. a killed xray outbound on 10810)
    //                  still blocks a new listen() until it expires.
    //
    // bind()-based probing is unreliable on Windows: a bind() to a wildcard address
    // is permitted to COEXIST with an existing wildcard/specific listener (verified
    // live — an exclusive bind to 0.0.0.0:10808 succeeded while xray already held it),
    // so it cannot detect occupancy across processes. The TCP table does not have
    // this blind spot. See docs/bugfix/2026-08-31-Bugfix-IsPortAvailable-Wildcard-v1.0.md.
    static bool isPortOccupiedInTable(int port) {
        bool occupied = false;

        // IPv4
        {
            DWORD size = 0;
            DWORD ret = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                            TCP_TABLE_OWNER_PID_ALL, 0);
            if (ret == ERROR_INSUFFICIENT_BUFFER && size > 0) {
                std::vector<unsigned char> buf(size);
                PMIB_TCPTABLE_OWNER_PID table =
                    reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
                if (GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                                        TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                        if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort))
                                == static_cast<u_short>(port)) {
                            DWORD st = table->table[i].dwState;
                            if (st == MIB_TCP_STATE_LISTEN
                                    || st == MIB_TCP_STATE_TIME_WAIT) {
                                occupied = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // IPv6
        if (!occupied) {
            DWORD size = 0;
            DWORD ret = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6,
                                            TCP_TABLE_OWNER_PID_ALL, 0);
            if (ret == ERROR_INSUFFICIENT_BUFFER && size > 0) {
                std::vector<unsigned char> buf(size);
                PMIB_TCP6TABLE_OWNER_PID table =
                    reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(buf.data());
                if (GetExtendedTcpTable(table, &size, FALSE, AF_INET6,
                                        TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                        if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort))
                                == static_cast<u_short>(port)) {
                            DWORD st = table->table[i].dwState;
                            if (st == MIB_TCP_STATE_LISTEN
                                    || st == MIB_TCP_STATE_TIME_WAIT) {
                                occupied = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        return occupied;
    }

    bool isPortAvailable(int port) {
        static bool wsaStarted = false;
        if (!wsaStarted) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                return false;
            }
            wsaStarted = true;
        }

        // Consult the real system TCP table (netstat-grade). The port is available
        // only if no endpoint currently holds it in LISTEN or TIME_WAIT. This catches
        // wildcard listeners (e.g. xray on 0.0.0.0 / [::]) AND lingering TIME_WAIT
        // sockets from a just-killed connection — the two historically broken cases.
        return !isPortOccupiedInTable(port);
    }

    int findAvailablePort(int startPort, int maxAttempts) {
        for (int i = 0; i < maxAttempts; ++i) {
            int port = startPort + i;
            if (port > 65535) break;
            if (isPortAvailable(port)) {
                return port;
            }
        }
        return -1;
    }

    // ---- Port-occupation diagnostics (2026-08-31 port investigation) ----
    static std::string tcpStateName(unsigned long st) {
        switch (st) {
            case MIB_TCP_STATE_CLOSED:     return "CLOSED";
            case MIB_TCP_STATE_LISTEN:     return "LISTEN";
            case MIB_TCP_STATE_SYN_SENT:   return "SYN_SENT";
            case MIB_TCP_STATE_SYN_RCVD:   return "SYN_RCVD";
            case MIB_TCP_STATE_ESTAB:      return "ESTABLISHED";
            case MIB_TCP_STATE_FIN_WAIT1:  return "FIN_WAIT1";
            case MIB_TCP_STATE_FIN_WAIT2:  return "FIN_WAIT2";
            case MIB_TCP_STATE_TIME_WAIT:  return "TIME_WAIT";
            case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
            case MIB_TCP_STATE_LAST_ACK:   return "LAST_ACK";
            case MIB_TCP_STATE_CLOSING:    return "CLOSING";
            default:                       return "STATE_" + std::to_string(st);
        }
    }

    static std::string resolveProcessName(DWORD pid) {
        std::string name = "<unknown>";
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (h == NULL) {
            return name;
        }
        char path[MAX_PATH] = {0};
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameA(h, 0, path, &len) && len > 0) {
            std::string p(path);
            size_t pos = p.find_last_of("\\/");
            name = (pos != std::string::npos) ? p.substr(pos + 1) : p;
        }
        CloseHandle(h);
        return name;
    }

    void logPortOccupants(int port) {
        auto dumpFamily = [&](ADDRESS_FAMILY af, bool v6) {
            DWORD size = 0;
            if (GetExtendedTcpTable(nullptr, &size, FALSE, af,
                                    TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER
                    || size == 0) {
                return;
            }
            std::vector<unsigned char> buf(size);
            if (v6) {
                PMIB_TCP6TABLE_OWNER_PID table =
                    reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(buf.data());
                if (GetExtendedTcpTable(table, &size, FALSE, af,
                                        TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
                    return;
                }
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort))
                            != static_cast<unsigned short>(port)) {
                        continue;
                    }
                    Logger::write("  port=" + std::to_string(port)
                                  + " pid=" + std::to_string(table->table[i].dwOwningPid)
                                  + " state=" + tcpStateName(table->table[i].dwState)
                                  + " process=" + resolveProcessName(table->table[i].dwOwningPid),
                                  LogLevel::INFO);
                }
            } else {
                PMIB_TCPTABLE_OWNER_PID table =
                    reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
                if (GetExtendedTcpTable(table, &size, FALSE, af,
                                        TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
                    return;
                }
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort))
                            != static_cast<unsigned short>(port)) {
                        continue;
                    }
                    Logger::write("  port=" + std::to_string(port)
                                  + " pid=" + std::to_string(table->table[i].dwOwningPid)
                                  + " state=" + tcpStateName(table->table[i].dwState)
                                  + " process=" + resolveProcessName(table->table[i].dwOwningPid),
                                  LogLevel::INFO);
                }
            }
        };
        dumpFamily(AF_INET, false);
        dumpFamily(AF_INET6, true);
    }

    // Helper: convert narrow string to wide string
    static std::wstring toWide(const std::string& s) {
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring wstr(static_cast<size_t>(len) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], len);
        return wstr;
    }

    bool isProcessRunning(const std::string& processName) {
        std::wstring wName = toWide(processName);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (wcsicmp(wName.c_str(), pe.szExeFile) == 0) {
                    CloseHandle(snapshot);
                    return true;
                }
            } while (Process32NextW(snapshot, &pe));
        }

        CloseHandle(snapshot);
        return false;
    }

    void killProcessByName(const std::string& processName) {
        std::wstring wName = toWide(processName);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }

        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (wcsicmp(wName.c_str(), pe.szExeFile) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess) {
                        TerminateProcess(hProcess, 1);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &pe));
        }

        CloseHandle(snapshot);
    }

    std::string getProcessNameFromPath(const std::string& fullPath) {
        std::filesystem::path p(fullPath);
        return p.filename().string();
    }

    bool isPrintableAscii(const std::string& s) {
        for (const char ch : s) {
            const unsigned char uc = static_cast<unsigned char>(ch);
            if (uc < 0x20 || uc > 0x7E) {
                return false;
            }
        }
        return true;
    }

    // Conservative domain garbage heuristics for subscription-imported addresses.
    // Rule 1: 0.* labels (covers 0.ir0.ir, 0.0.0.einetwork.news, etc.).
    // Rule 2: >=4 dot-separated labels that are single hex chars (covers IPv6
    // reversed-domain garbage such as 0.5.0.0.7.0.f.1.0.7.4.0.1.0.0.2.xzhi.eu.org).
    static bool isPublicDomain(const std::string& host) {
        if (host.size() >= 2 && host[0] == '0' && host[1] == '.') {
            return false;
        }

        size_t singleHexLabels = 0;
        size_t i = 0;
        while (i < host.size()) {
            size_t dotPos = host.find('.', i);
            std::string label;
            if (dotPos == std::string::npos) {
                label = host.substr(i);
                i = host.size();
            } else {
                label = host.substr(i, dotPos - i);
                i = dotPos + 1;
            }
            if (label.size() == 1) {
                const unsigned char c = static_cast<unsigned char>(label[0]);
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                    singleHexLabels++;
                }
            }
        }
        if (singleHexLabels >= 4) {
            return false;
        }
        return true;
    }

    bool isPublicAddress(const std::string& address) {
        if (address.empty()) {
            return false;
        }

        // IPv6 literal (contains ':')
        if (address.find(':') != std::string::npos) {
            if (address == "::1") return false;
            if (address == "::") return false;
            if (address.size() >= 2 && address[0] == 'f' && address[1] == 'f') return false;
            if (address.size() >= 2 && address[0] == 'f' && address[1] == 'c') return false;
            if (address.size() >= 2 && address[0] == 'f' && address[1] == 'd') return false;
            if (address.size() >= 4 && address[0] == 'f' && address[1] == 'e'
                && address[2] == '8' && address[3] == '0') return false;
            return true;
        }

        // No dots -> not an IPv4 address; treat as domain / single-label name.
        if (address.find('.') == std::string::npos) {
            if (!address.empty() && address[0] == '0') return false;
            return true;
        }

        // Try to parse as an IPv4 dotted-quad.  As soon as a character is
        // neither a digit nor a dot the whole string is a domain name and is
        // validated via isPublicDomain() instead.  Strings that remain purely
        // numeric/dotted are accepted only when they form a well-formed IPv4
        // address (exactly four octets, each 0..255, outside reserved ranges);
        // empty octets, octets > 255 or more than four octets are rejected.
        int octets[4] = {0, 0, 0, 0};
        int octetIndex = 0;
        int current = 0;
        bool inOctet = false;
        bool malformed = false;    // invalid IPv4 shape seen (scan continues)
        bool overflowed = false;   // current octet already > 255 (stop accumulating)

        for (const char ch : address) {
            if (ch >= '0' && ch <= '9') {
                if (octetIndex >= 4) {
                    malformed = true;              // more than four octets
                } else if (!overflowed) {
                    current = current * 10 + (ch - '0');
                    if (current > 255) {
                        overflowed = true;
                    }
                }
                inOctet = true;
            } else if (ch == '.') {
                if (!inOctet) malformed = true;    // empty octet (e.g. "1..2")
                if (overflowed) malformed = true;  // octet > 255
                if (octetIndex < 4) {
                    octets[octetIndex] = current;
                }
                octetIndex++;
                current = 0;
                overflowed = false;
                inOctet = false;
            } else {
                // Non-digit, non-dot -> domain name
                return isPublicDomain(address);
            }
        }

        if (!inOctet) return false;                // trailing dot (e.g. "1." / ".")
        if (overflowed) malformed = true;          // last octet > 255
        if (octetIndex != 3) return false;         // must be exactly four octets
        if (malformed) return false;
        octets[3] = current;

        // RFC1918 / special-use ranges (reject)
        if (octets[0] == 0) return false;                // 0.0.0.0/8
        if (octets[0] == 10) return false;               // 10.0.0.0/8
        if (octets[0] == 127) return false;              // 127.0.0.0/8
        if (octets[0] == 169 && octets[1] == 254) return false; // 169.254.0.0/16
        if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) return false; // 172.16/12
        if (octets[0] == 192 && octets[1] == 168) return false; // 192.168/16
        if (octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127) return false; // 100.64/10 CGNAT
        if (octets[0] >= 224 && octets[0] <= 239) return false; // 224/4 multicast
        if (octets[0] >= 240 && octets[0] <= 255) return false; // 240/4 reserved

        return true;
    }

    bool isValidUuid(const std::string& id) {
        if (id.size() != 36) return false;
        if (id[8] != '-' || id[13] != '-' || id[18] != '-' || id[23] != '-') {
            return false;
        }
        const char* p = id.c_str();
        for (int i = 0; i < 8; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
        }
        for (int i = 9; i < 13; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
        }
        for (int i = 14; i < 18; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
        }
        for (int i = 19; i < 23; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
        }
        for (int i = 24; i < 36; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
        }
        return true;
    }

    bool isSupportedSsCipher(const std::string& method) {
        static const char* supported[] = {
            "aes-128-gcm",
            "aes-256-gcm",
            "chacha20-poly1305",
            "chacha20-ietf-poly1305",
            "xchacha20-poly1305",
            "none",
            "2022-blake3-aes-128-gcm",
            "2022-blake3-aes-256-gcm",
            "2022-blake3-chacha20-poly1305"
        };
        size_t count = sizeof(supported) / sizeof(supported[0]);
        for (size_t i = 0; i < count; ++i) {
            if (method == supported[i]) {
                return true;
            }
        }
        return false;
    }

    bool isTestResultValid(bool success, long latencyMs) {
        return success && latencyMs > 0;
    }

    bool isDelayValid(const std::string& delayStr) {
        if (delayStr.empty() || delayStr == "-1") return false;
        try {
            long val = std::stol(delayStr);
            return val > 0;
        } catch (...) {
            return false;
        }
    }

    ProxyListMaps buildProxyListMaps(const std::vector<db::models::ProfileExItem>& exItems) {
        ProxyListMaps maps;
        for (const db::models::ProfileExItem& ex : exItems) {
            maps.delayMap[ex.indexid] = ex.delay;
            maps.messageMap[ex.indexid] = ex.message;
            maps.failuresMap[ex.indexid] = ex.consecutive_failures;
            maps.startCountMap[ex.indexid] = ex.start_count;

            if (ex.total_runtime_ms > 0) {
                maps.runtimeMap[ex.indexid] = ex.total_runtime_ms;
            } else {
                maps.runtimeMap[ex.indexid] = 0;
            }

            int stable = ex.start_count - ex.crash_count;
            if (stable < 0) stable = 0;
            if (ex.start_count == 0) {
                maps.healthMap[ex.indexid] = 0.0;
            } else {
                maps.healthMap[ex.indexid] = static_cast<double>(stable + 1) /
                                             static_cast<double>(ex.start_count + 2);
            }
        }
        return maps;
    }
}