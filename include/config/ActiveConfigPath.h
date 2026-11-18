#ifndef CONFIG_ACTIVE_CONFIG_PATH_H
#define CONFIG_ACTIVE_CONFIG_PATH_H

#include <string>

namespace config {

// 进程级生效配置路径：
//   - main_gui / main_cli 在参数解析完成后调用 setActiveConfigPath(...)；
//   - AppController::saveConfig 与 ConfigDialog 均用 activeConfigPath()，
//     保证 "-c/--config" 沙箱真正生效（不再写死到 bin/config.json）。
// 未设置时回落 ConfigReader::getDefaultConfigPath()（与普通运行时一致）。
void setActiveConfigPath(const std::string& path);
const std::string& activeConfigPath();

}  // namespace config
#endif  // CONFIG_ACTIVE_CONFIG_PATH_H
