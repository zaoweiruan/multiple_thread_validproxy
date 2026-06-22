#include "config/ConfigPathResolver.h"
#include "Utils.h"

#include <filesystem>

namespace config {

ConfigPathResolver::ConfigPathResolver(const std::string& exeDir)
    : exeDir_(exeDir)
{
}

std::string ConfigPathResolver::resolve(const std::string& relativePath) const {
    if (relativePath.empty()) {
        return std::string();
    }
    std::filesystem::path p(relativePath);
    if (p.is_absolute()) {
        return relativePath;
    }
    return (std::filesystem::path(exeDir_) / p).string();
}

std::string ConfigPathResolver::defaultConfigPath() const {
    return (std::filesystem::path(exeDir_) / ".." / "bin" / "config.json").string();
}

std::string ConfigPathResolver::exeDir() const {
    return exeDir_;
}

std::string ConfigPathResolver::detectExeDir() {
    return utils::getExecutableDir();
}

} // namespace config
