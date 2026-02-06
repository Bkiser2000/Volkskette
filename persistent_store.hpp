#ifndef PERSISTENT_STORE_HPP
#define PERSISTENT_STORE_HPP

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Forward declarations
struct Block;
struct Transaction;
class SmartContract;

class PersistentStore {
private:
    std::string storage_dir_;
    std::string blocks_file_;
    std::string contracts_file_;
    std::string state_file_;
    
    bool ensure_directory_exists();
    bool file_exists(const std::string& path) const;

public:
    PersistentStore(const std::string& storage_dir = "./blockchain_data");
    ~PersistentStore();
    
    // Block operations
    bool save_block(const json& block_json);
    bool save_blocks(const std::vector<json>& blocks_json);
    std::vector<json> load_blocks() const;
    
    // Contract operations
    bool save_contract(const json& contract_json);
    bool save_contracts(const std::vector<json>& contracts_json);
    std::vector<json> load_contracts() const;
    
    // Account state
    bool save_account_state(const json& state_json);
    json load_account_state();
    
    // General export/import
    bool export_blockchain_state(const json& full_state);
    json import_blockchain_state();
    
    // Utilities
    void clear_all_data();
    bool has_saved_data() const;
    std::string get_storage_dir() const { return storage_dir_; }
    
    // Statistics
    int get_block_count();
    int get_contract_count();
    size_t get_total_storage_size();
};

#endif // PERSISTENT_STORE_HPP
