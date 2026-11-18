#pragma once

// ---------------------------------------------------------------------------
// AutoTaskStatePath — 自动任务断点文件路径解析（纯函数，可单测）
//
// 背景（2026-11-18 现场问题）：
//   `bin/config.json` 的 `auto_task.state_file` 是**用户机器的绝对路径**
//   （如 `E:\eclipse_workspace\...\bin/worker/autotask_state.json`）。把整个
//   `bin/` 目录拷到另一台电脑后，该盘符/目录不存在，`create_directories` 失败 →
//   每个步骤都刷一条
//     `[ERROR] AutoTask: failed to write state file: E:\...`
//
// 本模块提供可移植的路径解析规则：
//   1. 未配置（空串）           → `<exe 目录>/worker/autotask_state.json`
//   2. 配置为相对路径           → 以 **exe 目录**为基准拼接（跟着程序走）
//   3. 配置为绝对路径（盘符/UNC）→ 原样使用（是否可用由运行时探测决定）
// 运行时若目标父目录无法创建/不可用，则回退到规则 1 并打一次 WARN，
// 不再每一步刷 ERROR（见 AutoTaskManager 构造函数）。
// ---------------------------------------------------------------------------

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace autotask {

inline std::string trimCopy(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

inline std::string trimTrailingSeparators(const std::string& text) {
    std::string result = text;
    while (!result.empty() && (result.back() == '/' || result.back() == '\\')) {
        result.pop_back();
    }
    return result;
}

// 盘符绝对路径（C:...）、UNC（\\server\share）或根相对路径（\foo、/foo）。
inline bool isAbsolutePath(const std::string& path) {
    if (path.size() >= 2) {
        const char drive = path[0];
        const bool isDriveLetter =
            ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z'));
        if (isDriveLetter && path[1] == ':') {
            return true;
        }
        if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/')) {
            return true;  // UNC
        }
    }
    if (!path.empty() && (path[0] == '\\' || path[0] == '/')) {
        return true;  // 根相对（当前驱动器根）
    }
    return false;
}

// 默认断点文件：跟随 exe 所在目录（发布包解压到任意位置都能用）。
inline std::string defaultStateFilePath(const std::string& exeDir) {
    const std::string base = trimTrailingSeparators(exeDir);
    if (base.empty()) {
        return std::string("worker/autotask_state.json");
    }
    return base + "/worker/autotask_state.json";
}

// 按上述三条规则解析配置值。仅做字符串处理，不触碰文件系统。
inline std::string resolveStateFilePath(const std::string& configured, const std::string& exeDir) {
    const std::string value = trimCopy(configured);
    if (value.empty()) {
        return defaultStateFilePath(exeDir);
    }
    if (isAbsolutePath(value)) {
        return value;
    }
    const std::string base = trimTrailingSeparators(exeDir);
    if (base.empty()) {
        return value;
    }
    return base + "/" + value;
}

// 确保目标文件的父目录存在；返回 false 表示该路径在本机不可用
// （盘符不存在、父路径是文件、无权限等）。
inline bool ensureParentDirectory(const std::string& filePath) {
    if (filePath.empty()) {
        return false;
    }
    const std::filesystem::path target(filePath);
    const std::filesystem::path parent = target.parent_path();
    if (parent.empty()) {
        return false;
    }
    std::error_code ec;
    if (std::filesystem::is_directory(parent, ec)) {
        return true;
    }
    if (std::filesystem::exists(parent, ec)) {
        return false;  // 存在但不是目录（例如同名文件）
    }
    ec.clear();
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return false;
    }
    ec.clear();
    return std::filesystem::is_directory(parent, ec);
}

}  // namespace autotask
