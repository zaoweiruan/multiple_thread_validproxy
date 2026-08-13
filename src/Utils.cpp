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

namespace utils {
    std::string getCurrentTimestamp() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return std::to_string(timestamp);
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

    bool isPortAvailable(int port) {
        static bool wsaStarted = false;
        if (!wsaStarted) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                return false;
            }
            wsaStarted = true;
        }

        // Use connect() instead of bind() because Windows allows overlapping address
        // bindings (e.g., xray on 0.0.0.0:10808 vs our check on 127.0.0.1:10808).
        // connect() to 127.0.0.1 detects any listener on that port regardless of
        // the address the server bound to.
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            return false;
        }

        // Non-blocking so we can control connect() timeout
        u_long nonblocking = 1;
        ioctlsocket(sock, FIONBIO, &nonblocking);

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port));

        int result = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        // Connected immediately — port is occupied
        if (result == 0) {
            closesocket(sock);
            return false;
        }

        int err = WSAGetLastError();

        // Immediately refused — nothing is listening
        if (err == WSAECONNREFUSED) {
            closesocket(sock);
            return true;
        }

        // Connection in progress — wait with short timeout
        if (err == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(sock, &writeSet);

            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 30000;  // 30 ms — the probe only ever connects to
                                 // 127.0.0.1, where connect resolves in
                                 // microseconds; 200 ms per probe was pure waste

            int selResult = select(0, nullptr, &writeSet, nullptr, &tv);

            if (selResult == 1) {
                // Socket became writable — check if connected or refused
                int optval = 0;
                socklen_t optlen = sizeof(optval);
                getsockopt(sock, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&optval), &optlen);
                bool occupied = (optval == 0);
                closesocket(sock);
                return !occupied;
            }

            // Timeout or error — assume free
            closesocket(sock);
            return true;
        }

        // Any other error — assume available
        closesocket(sock);
        return true;
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
 }