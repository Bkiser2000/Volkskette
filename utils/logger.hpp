#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    CRITICAL = 4
};

class Logger {
private:
    static std::mutex mutex_;
    
    LogLevel current_level_;
    std::ofstream file_stream_;
    bool file_enabled_;
    bool console_enabled_;
    std::string log_file_;
    
    Logger();
    
    static std::string level_to_string(LogLevel level);
    static std::string get_timestamp();
    static std::string get_color_code(LogLevel level);
    static std::string reset_color();

public:
    ~Logger();
    
    // Singleton access
    static Logger& getInstance();
    
    // Configuration
    static void enable_file_logging(const std::string& filepath);
    static void disable_file_logging();
    static void enable_console_logging();
    static void disable_console_logging();
    static void set_level(LogLevel level);
    static LogLevel get_level();
    
    // Logging methods
    static void debug(const std::string& module, const std::string& message);
    static void info(const std::string& module, const std::string& message);
    static void warn(const std::string& module, const std::string& message);
    static void error(const std::string& module, const std::string& message);
    static void critical(const std::string& module, const std::string& message);
    
    // Internal logging (with level parameter)
    static void log(LogLevel level, const std::string& module, 
                   const std::string& message);
};

// Convenience macros
#define LOG_DEBUG(module, msg) Logger::debug(module, msg)
#define LOG_INFO(module, msg) Logger::info(module, msg)
#define LOG_WARN(module, msg) Logger::warn(module, msg)
#define LOG_ERROR(module, msg) Logger::error(module, msg)
#define LOG_CRITICAL(module, msg) Logger::critical(module, msg)

#endif // LOGGER_HPP
