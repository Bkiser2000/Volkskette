#include "logger.hpp"

// Static member initialization
std::mutex Logger::mutex_;

Logger::Logger() : current_level_(LogLevel::INFO), file_enabled_(false), 
                   console_enabled_(true) {
}

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

Logger& Logger::getInstance() {
    static Logger instance;  // Meyer's Singleton - thread-safe in C++11+
    return instance;
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_color_code(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "\033[36m";     // Cyan
        case LogLevel::INFO: return "\033[32m";      // Green
        case LogLevel::WARN: return "\033[33m";      // Yellow
        case LogLevel::ERROR: return "\033[31m";     // Red
        case LogLevel::CRITICAL: return "\033[35m";  // Magenta
        default: return "";
    }
}

std::string Logger::reset_color() {
    return "\033[0m";
}

void Logger::enable_file_logging(const std::string& filepath) {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (logger.file_stream_.is_open()) {
        logger.file_stream_.close();
    }
    
    logger.log_file_ = filepath;
    logger.file_stream_.open(filepath, std::ios::app);
    logger.file_enabled_ = logger.file_stream_.is_open();
    
    if (logger.file_enabled_) {
        info("Logger", "File logging enabled: " + filepath);
    }
}

void Logger::disable_file_logging() {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (logger.file_stream_.is_open()) {
        logger.file_stream_.close();
    }
    logger.file_enabled_ = false;
}

void Logger::enable_console_logging() {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    logger.console_enabled_ = true;
}

void Logger::disable_console_logging() {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    logger.console_enabled_ = false;
}

void Logger::set_level(LogLevel level) {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    logger.current_level_ = level;
}

LogLevel Logger::get_level() {
    Logger& logger = getInstance();
    std::lock_guard<std::mutex> lock(mutex_);
    return logger.current_level_;
}

void Logger::log(LogLevel level, const std::string& module, 
                 const std::string& message) {
    Logger& logger = getInstance();
    
    // Check if we should log this level
    if (level < logger.current_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string timestamp = get_timestamp();
    std::string level_str = level_to_string(level);
    std::string log_line = "[" + timestamp + "] [" + level_str + "] [" + 
                          module + "] " + message;
    
    // Console output with colors
    if (logger.console_enabled_) {
        std::string color = get_color_code(level);
        std::cout << color << log_line << reset_color() << std::endl;
    }
    
    // File output
    if (logger.file_enabled_ && logger.file_stream_.is_open()) {
        logger.file_stream_ << log_line << std::endl;
        logger.file_stream_.flush();
    }
}

void Logger::debug(const std::string& module, const std::string& message) {
    log(LogLevel::DEBUG, module, message);
}

void Logger::info(const std::string& module, const std::string& message) {
    log(LogLevel::INFO, module, message);
}

void Logger::warn(const std::string& module, const std::string& message) {
    log(LogLevel::WARN, module, message);
}

void Logger::error(const std::string& module, const std::string& message) {
    log(LogLevel::ERROR, module, message);
}

void Logger::critical(const std::string& module, const std::string& message) {
    log(LogLevel::CRITICAL, module, message);
}
