#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    static void init(const std::string& logDir, const std::string& prefix);
    static void write(const std::string& msg);
    static void writeTimestamp(const std::string& msg);
    static void flush();
    static void close();
    static bool isEnabled();
    static std::ofstream* getFile();
    static std::string getLogDir();
    static std::string getPrefix();

private:
    static std::string logDir_;
    static std::string prefix_;
    static std::ofstream* outFile_;
    static std::mutex mutex_;
    static bool enabled_;
};

#endif // LOGGER_H