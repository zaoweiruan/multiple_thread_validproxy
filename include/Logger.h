#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    LOG_ERROR = 4
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
    static bool isEnabled();
    static std::ofstream* getFile();
    static std::string getLogDir();
    static std::string getPrefix();
    static void setLevel(LogLevel level);
    static void setFileLevel(LogLevel level);
    static void setConsoleLevel(LogLevel level);
    static LogLevel getLevel();
    static LogLevel getFileLevel();
    static LogLevel getConsoleLevel();
    static std::string levelToString(LogLevel level);
    static LogLevel stringToLevel(const std::string& str);

private:
    static std::string logDir_;
    static std::string prefix_;
    static std::ofstream* outFile_;
    static std::mutex mutex_;
    static bool enabled_;
    static LogLevel fileLevel_;
    static LogLevel consoleLevel_;
};

#endif // LOGGER_H