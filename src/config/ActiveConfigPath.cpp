#include "config/ActiveConfigPath.h"

#include "ConfigReader.h"

#include <mutex>
#include <string>

namespace config {
namespace {

std::mutex g_mutex;
std::string g_path;

}  // namespace

void setActiveConfigPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
}

const std::string& activeConfigPath() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_path.empty()) {
        // 未设置：回落到 ConfigReader 的默认路径（与普通运行时一致）。
        static std::string fallback;
        if (fallback.empty()) {
            fallback = ConfigReader::getDefaultConfigPath();
        }
        return fallback;
    }
    return g_path;
}

}  // namespace config
