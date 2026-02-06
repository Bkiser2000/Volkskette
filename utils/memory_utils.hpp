#ifndef MEMORY_UTILS_HPP
#define MEMORY_UTILS_HPP

#include <nlohmann/json.hpp>
#include <vector>
#include <string>

using json = nlohmann::json;

/**
 * Memory optimization utilities for efficient allocation and moves
 */
namespace memory_utils {
    
    /**
     * Reserve capacity for vector to avoid reallocations
     * Usage: auto vec = reserve_vector<MyType>(expected_size);
     */
    template<typename T>
    std::vector<T> reserve_vector(size_t capacity) {
        std::vector<T> v;
        v.reserve(capacity);
        return v;
    }
    
    /**
     * Efficiently move JSON to string (avoids copies)
     */
    inline std::string json_to_string(json& j) {
        // Move string from JSON dump (C++17 move semantics)
        return j.dump();
    }
    
    /**
     * Clear vector and free memory
     */
    template<typename T>
    inline void clear_and_shrink(std::vector<T>& v) {
        std::vector<T>().swap(v);  // Clear and free memory
    }
    
    /**
     * Memory pool allocator for frequently allocated small objects
     */
    template<typename T>
    class ObjectPool {
    private:
        std::vector<T*> available_;
        std::vector<T*> in_use_;
        size_t max_size_;
        
    public:
        explicit ObjectPool(size_t max_size = 1000) : max_size_(max_size) {
            available_.reserve(max_size);
        }
        
        ~ObjectPool() {
            for (auto obj : available_) delete obj;
            for (auto obj : in_use_) delete obj;
        }
        
        T* acquire() {
            if (!available_.empty()) {
                T* obj = available_.back();
                available_.pop_back();
                in_use_.push_back(obj);
                return obj;
            }
            
            if (in_use_.size() < max_size_) {
                T* obj = new T();
                in_use_.push_back(obj);
                return obj;
            }
            
            return nullptr;
        }
        
        void release(T* obj) {
            auto it = std::find(in_use_.begin(), in_use_.end(), obj);
            if (it != in_use_.end()) {
                in_use_.erase(it);
                available_.push_back(obj);
            }
        }
        
        size_t available_count() const { return available_.size(); }
        size_t in_use_count() const { return in_use_.size(); }
    };
    
} // namespace memory_utils

#endif // MEMORY_UTILS_HPP
