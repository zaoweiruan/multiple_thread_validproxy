#ifndef CONFIG_PATH_RESOLVER_H
#define CONFIG_PATH_RESOLVER_H

#include <string>

namespace config {

class ConfigPathResolver {
public:
    explicit ConfigPathResolver(const std::string& exeDir);

    std::string resolve(const std::string& relativePath) const;
    std::string defaultConfigPath() const;
    std::string exeDir() const;

    static std::string detectExeDir();

private:
    std::string exeDir_;
};

} // namespace config

#endif // CONFIG_PATH_RESOLVER_H
