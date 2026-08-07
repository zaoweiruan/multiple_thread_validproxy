#ifndef LOGGERINSTANCE_H
#define LOGGERINSTANCE_H

#include <string>
#include <fstream>
#include <mutex>
#include <functional>
#include <vector>

#include "Logger.h"

class LoggerInstance {
public:
    LoggerInstance();
    ~LoggerInstance();

    void init(const std::string& logDir, const std::string& prefix);
    void init(const std::string& logDir, const std::string& prefix, LogLevel fileLevel, LogLevel consoleLevel);
    void write(const std::string& msg);
    void write(const std::string& msg, LogLevel level);
    void writeTimestamp(const std::string& msg);
    void writeTimestamp(const std::string& msg, LogLevel level);
    void flush();
    void close();

    using LogCallback = Logger::LogCallback;
    void setLogCallback(LogCallback cb);
    void clearLogCallback();
    void pushCallback(LogCallback cb);
    void popCallback();

    bool isEnabled() const;
    std::ofstream* getFile();
    std::string getFilePath() const;
    std::string getLogDir() const;
    std::string getPrefix() const;
    void setLevel(LogLevel level);
    void setFileLevel(LogLevel level);
    void setConsoleLevel(LogLevel level);
    void setFileEnabled(bool enabled);
    bool isFileEnabled() const;
    void setConsoleEnabled(bool enabled);
    LogLevel getLevel() const;
    LogLevel getFileLevel() const;
    LogLevel getConsoleLevel() const;
    static std::string levelToString(LogLevel level);
    static LogLevel stringToLevel(const std::string& str);
    void disableFile();
    void enableConsoleOnly();

private:
    std::string logDir_;
    std::string prefix_;
    std::string filePath_;
    std::ofstream* outFile_;
    mutable std::mutex mutex_;
    bool enabled_;
    bool fileEnabled_;
    bool consoleEnabled_;
    LogLevel fileLevel_;
    LogLevel consoleLevel_;
    LogCallback logCallback_;
    std::vector<LogCallback> callbackStack_;
    mutable std::mutex callbackMutex_;
};

#endif
