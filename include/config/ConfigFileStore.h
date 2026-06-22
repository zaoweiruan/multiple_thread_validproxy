#ifndef CONFIG_FILE_STORE_H
#define CONFIG_FILE_STORE_H

#include <string>

namespace config {

class ConfigFileStore {
public:
    std::string read(const std::string& path);
    void write(const std::string& path, const std::string& content);
};

} // namespace config

#endif // CONFIG_CONFIGFILESTORE_H
