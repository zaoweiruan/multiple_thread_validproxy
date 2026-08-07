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
 }