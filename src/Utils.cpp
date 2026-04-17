#include "Utils.h"
#include <windows.h>
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

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
        NOTIFYICONDATA nid = {0};
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        
        strncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
        strncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
        
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
}