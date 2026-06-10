#include "Utils.h"
#include <windows.h>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace utils {
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
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
}