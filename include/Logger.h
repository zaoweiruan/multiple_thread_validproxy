#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <functional>
#include <vector>

class LoggerInstance;

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    REPORT = 3,
    WARN = 4,
    ERR = 5
};

class Logger {
public:
    static void init(const std::string& logDir, const std::string& prefix);
    static void init(const std::string& logDir, const std::string& prefix, LogLevel fileLevel, LogLevel consoleLevel);
    static void write(const std::string& msg);
    static void write(const std::string& msg, LogLevel level);
    static void writeTimestamp(const std::string& msg);
    static void writeTimestamp(const std::string& msg, LogLevel level);
    static void flush();
    static void close();
    // Callback for UI integration — single consumer API
    using LogCallback = std::function<void(const std::string&, LogLevel)>;
    static void setLogCallback(LogCallback cb);   // 覆盖式；初始化时使用
    static void clearLogCallback();                // 清空

    // Stack-based callback for multiple concurrent consumers (RAII save/restore)
    // pushCallback(): 压入 cb，cb 成为当前激活回调（同时保留前序者在栈中）
    // popCallback():  弹出顶层，前一个回调自动恢复（若无则 nullptr）
    static void pushCallback(LogCallback cb);
    static void popCallback();

    static bool isEnabled();
    static std::ofstream* getFile();
    static std::string getLogDir();
    static std::string getPrefix();
    static void setLevel(LogLevel level);
    static void setFileLevel(LogLevel level);
    static void setConsoleLevel(LogLevel level);
    static void setFileEnabled(bool enabled);
    static bool isFileEnabled();
    static void setConsoleEnabled(bool enabled);
    static LogLevel getLevel();
    static LogLevel getFileLevel();
    static LogLevel getConsoleLevel();
static std::string levelToString(LogLevel level);
    static LogLevel stringToLevel(const std::string& str);
    static void disableFile();
    static void enableConsoleOnly();

    static LoggerInstance& defaultInstance();

 private:
     static LoggerInstance* defaultInstance_;
 };

#endif // LOGGER_H