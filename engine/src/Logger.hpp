#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <deque>
#include <sstream>

enum class LogLevel {
    LevelInfo,
    LevelWarning,
    LevelError
};

struct LogEntry {
    LogLevel level;
    std::string message;
    // timestamp could be added here
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        
        // Console output
        switch (level) {
            case LogLevel::LevelInfo: std::cout << "[INFO] "; break;
            case LogLevel::LevelWarning: std::cerr << "[WARN] "; break;
            case LogLevel::LevelError: std::cerr << "[ERR] "; break;
        }
        std::cout << message << std::endl;

        // Store for GUI
        entries.push_back({level, message});
        if (entries.size() > maxEntries) {
            entries.pop_front();
        }
    }

    // Helper methods for easy logging
    static void info(const std::string& message) { getInstance().log(LogLevel::LevelInfo, message); }
    static void warn(const std::string& message) { getInstance().log(LogLevel::LevelWarning, message); }
    static void error(const std::string& message) { getInstance().log(LogLevel::LevelError, message); }

    std::vector<LogEntry> getEntries() const {
        std::lock_guard<std::mutex> lock(mutex);
        return {entries.begin(), entries.end()};
    }

private:
    Logger() {}
    ~Logger() {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::deque<LogEntry> entries;
    mutable std::mutex mutex;
    const size_t maxEntries = 50; // Keep last 50 logs for display
};
