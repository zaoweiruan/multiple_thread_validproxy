#include "Logger.h"
#include "LoggerInstance.h"

LoggerInstance* Logger::defaultInstance_ = nullptr;

LoggerInstance& Logger::defaultInstance() {
    if (!defaultInstance_) {
        defaultInstance_ = new LoggerInstance();
    }
    return *defaultInstance_;
}

void Logger::init(const std::string& logDir, const std::string& prefix) {
    defaultInstance().init(logDir, prefix);
}

void Logger::init(const std::string& logDir, const std::string& prefix, LogLevel fileLevel, LogLevel consoleLevel) {
    defaultInstance().init(logDir, prefix, fileLevel, consoleLevel);
}

void Logger::write(const std::string& msg) {
    defaultInstance().write(msg);
}

void Logger::write(const std::string& msg, LogLevel level) {
    defaultInstance().write(msg, level);
}

void Logger::writeTimestamp(const std::string& msg) {
    defaultInstance().writeTimestamp(msg);
}

void Logger::writeTimestamp(const std::string& msg, LogLevel level) {
    defaultInstance().writeTimestamp(msg, level);
}

void Logger::flush() {
    defaultInstance().flush();
}

void Logger::close() {
    defaultInstance().close();
}

void Logger::setLogCallback(LogCallback cb) {
    defaultInstance().setLogCallback(std::move(cb));
}

void Logger::clearLogCallback() {
    defaultInstance().clearLogCallback();
}

void Logger::pushCallback(LogCallback cb) {
    defaultInstance().pushCallback(std::move(cb));
}

void Logger::popCallback() {
    defaultInstance().popCallback();
}

bool Logger::isEnabled() {
    return defaultInstance().isEnabled();
}

std::ofstream* Logger::getFile() {
    return defaultInstance().getFile();
}

std::string Logger::getLogDir() {
    return defaultInstance().getLogDir();
}

std::string Logger::getPrefix() {
    return defaultInstance().getPrefix();
}

void Logger::setLevel(LogLevel level) {
    defaultInstance().setLevel(level);
}

void Logger::setFileLevel(LogLevel level) {
    defaultInstance().setFileLevel(level);
}

void Logger::setConsoleLevel(LogLevel level) {
    defaultInstance().setConsoleLevel(level);
}

void Logger::setFileEnabled(bool enabled) {
    defaultInstance().setFileEnabled(enabled);
}

bool Logger::isFileEnabled() {
    return defaultInstance().isFileEnabled();
}

void Logger::setConsoleEnabled(bool enabled) {
    defaultInstance().setConsoleEnabled(enabled);
}

LogLevel Logger::getLevel() {
    return defaultInstance().getLevel();
}

LogLevel Logger::getFileLevel() {
    return defaultInstance().getFileLevel();
}

LogLevel Logger::getConsoleLevel() {
    return defaultInstance().getConsoleLevel();
}

std::string Logger::levelToString(LogLevel level) {
    return LoggerInstance::levelToString(level);
}

LogLevel Logger::stringToLevel(const std::string& str) {
    return LoggerInstance::stringToLevel(str);
}

void Logger::disableFile() {
    defaultInstance().disableFile();
}

void Logger::enableConsoleOnly() {
    defaultInstance().enableConsoleOnly();
}
