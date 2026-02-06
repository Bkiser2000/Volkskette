#ifndef MEMORY_MONITOR_HPP
#define MEMORY_MONITOR_HPP

#include <cstddef>
#include <string>
#include <map>
#include <mutex>

/**
 * Memory monitoring and profiling utilities
 * Tracks allocation sizes and patterns without runtime overhead in production
 */
class MemoryMonitor {
private:
    static constexpr bool ENABLED = true;  // Can be disabled for production
    
    size_t total_allocated_ = 0;
    size_t total_freed_ = 0;
    size_t peak_usage_ = 0;
    size_t current_usage_ = 0;
    std::map<std::string, size_t> category_usage_;
    mutable std::mutex mutex_;
    
    MemoryMonitor() = default;
    
public:
    static MemoryMonitor& instance() {
        static MemoryMonitor monitor;
        return monitor;
    }
    
    void record_allocation(const std::string& category, size_t bytes) {
        if (!ENABLED) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        total_allocated_ += bytes;
        current_usage_ += bytes;
        category_usage_[category] += bytes;
        
        if (current_usage_ > peak_usage_) {
            peak_usage_ = current_usage_;
        }
    }
    
    void record_deallocation(size_t bytes) {
        if (!ENABLED) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        total_freed_ += bytes;
        current_usage_ = (current_usage_ > bytes) ? current_usage_ - bytes : 0;
    }
    
    struct MemoryStats {
        size_t total_allocated;
        size_t total_freed;
        size_t current_usage;
        size_t peak_usage;
        std::map<std::string, size_t> by_category;
    };
    
    MemoryStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            total_allocated_,
            total_freed_,
            current_usage_,
            peak_usage_,
            category_usage_
        };
    }
    
    void print_summary() const {
        if (!ENABLED) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "\n=== Memory Usage Summary ===\n";
        std::cout << "Total Allocated: " << (total_allocated_ / 1024) << " KB\n";
        std::cout << "Total Freed: " << (total_freed_ / 1024) << " KB\n";
        std::cout << "Current Usage: " << (current_usage_ / 1024) << " KB\n";
        std::cout << "Peak Usage: " << (peak_usage_ / 1024) << " KB\n";
        
        std::cout << "\nUsage by Category:\n";
        for (const auto& [category, bytes] : category_usage_) {
            std::cout << "  " << category << ": " << (bytes / 1024) << " KB\n";
        }
        std::cout << std::endl;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        total_allocated_ = 0;
        total_freed_ = 0;
        peak_usage_ = 0;
        current_usage_ = 0;
        category_usage_.clear();
    }
};

#endif // MEMORY_MONITOR_HPP
