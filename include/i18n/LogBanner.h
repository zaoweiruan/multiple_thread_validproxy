#ifndef I18N_LOG_BANNER_H
#define I18N_LOG_BANNER_H

#include <string>

namespace i18n {
namespace LogBanner {

// 会话语言横幅（Spec 2026-11-18 §7.7.1）：
//   - 每个日志文件开头写 "===== session language: <code> ====="
//   - 若上一会话语言不同，额外写 "----- UI language changed since last session: <prev> -> <curr> -----"
// 两条都是 INFO 级别、语言中立（只含语言码与分隔符）。
std::string readPreviousLanguage(const std::string& logDir, const std::string& currentLogPath);

void writeSessionBanner(const std::string& logDir, const std::string& currentLogPath,
                       const std::string& currentCode);

}  // namespace LogBanner
}  // namespace i18n

#endif  // I18N_LOG_BANNER_H
