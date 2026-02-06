#include "persistent_store.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

PersistentStore::PersistentStore(const std::string& storage_dir)
    : storage_dir_(storage_dir) {
    
    blocks_file_ = storage_dir_ + "/blocks.json";
    contracts_file_ = storage_dir_ + "/contracts.json";
    state_file_ = storage_dir_ + "/state.json";
    
    if (!ensure_directory_exists()) {
        std::cerr << "Warning: Failed to create storage directory: " << storage_dir_ << std::endl;
    } else {
        std::cerr << "Storage initialized at: " << storage_dir_ << std::endl;
    }
}

PersistentStore::~PersistentStore() {
    // Cleanup if needed
}

bool PersistentStore::ensure_directory_exists() {
    // Check if directory exists
    struct stat st = {};
    if (stat(storage_dir_.c_str(), &st) == -1) {
        // Directory doesn't exist, create it
        if (mkdir(storage_dir_.c_str(), 0755) == -1) {
            std::cerr << "Failed to create directory: " << storage_dir_ << std::endl;
            return false;
        }
        std::cerr << "Created storage directory: " << storage_dir_ << std::endl;
    }
    return true;
}

bool PersistentStore::file_exists(const std::string& path) const {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool PersistentStore::save_block(const json& block_json) {
    try {
        std::vector<json> blocks = load_blocks();
        blocks.push_back(block_json);
        
        std::ofstream f(blocks_file_);
        f << json(blocks).dump(4);
        f.close();
        
        // Block saved successfully
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving block: " << e.what() << std::endl;
        return false;
    }
}

bool PersistentStore::save_blocks(const std::vector<json>& blocks_json) {
    try {
        std::ofstream f(blocks_file_);
        f << json(blocks_json).dump(4);
        f.close();
        
        std::cerr << "Saved " << blocks_json.size() << " blocks to storage" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving blocks: " << e.what() << std::endl;
        return false;
    }
}

std::vector<json> PersistentStore::load_blocks() const {
    std::vector<json> blocks;
    
    if (!file_exists(blocks_file_)) {
        LOG_DEBUG("PersistentStore", "No blocks file found - starting fresh");
        return blocks;
    }
    
    try {
        std::ifstream f(blocks_file_);
        json data;
        f >> data;
        f.close();
        
        if (data.is_array()) {
            blocks = data.get<std::vector<json>>();
        }
        
        LOG_INFO("PersistentStore", "Loaded " + std::to_string(blocks.size()) + 
                 " blocks from storage");
    } catch (const std::exception& e) {
        LOG_WARN("PersistentStore", "Error loading blocks: " + std::string(e.what()));
    }
    
    return blocks;
}

bool PersistentStore::save_contract(const json& contract_json) {
    try {
        std::vector<json> contracts = load_contracts();
        contracts.push_back(contract_json);
        
        std::ofstream f(contracts_file_);
        f << json(contracts).dump(4);
        f.close();
        
        // Contract saved successfully
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving contract: " << e.what() << std::endl;
        return false;
    }
}

bool PersistentStore::save_contracts(const std::vector<json>& contracts_json) {
    try {
        std::ofstream f(contracts_file_);
        f << json(contracts_json).dump(4);
        f.close();
        
        std::cerr << "Saved " << contracts_json.size() << " contracts to storage" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving contracts: " << e.what() << std::endl;
        return false;
    }
}

std::vector<json> PersistentStore::load_contracts() const {
    std::vector<json> contracts;
    
    if (!file_exists(contracts_file_)) {
        LOG_DEBUG("PersistentStore", "No contracts file found");
        return contracts;
    }
    
    try {
        std::ifstream f(contracts_file_);
        json data;
        f >> data;
        f.close();
        
        if (data.is_array()) {
            contracts = data.get<std::vector<json>>();
        }
        
        std::cerr << "Loaded " << contracts.size() << " contracts from storage" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading contracts: " << e.what() << std::endl;
    }
    
    return contracts;
}

bool PersistentStore::save_account_state(const json& state_json) {
    try {
        std::ofstream f(state_file_);
        f << state_json.dump(4);
        f.close();
        
        // Account state saved
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving account state: " << e.what() << std::endl;
        return false;
    }
}

json PersistentStore::load_account_state() {
    if (!file_exists(state_file_)) {
        // No account state file found
        return json::object();
    }
    
    try {
        std::ifstream f(state_file_);
        json data;
        f >> data;
        f.close();
        
        std::cerr << "Loaded account state from storage" << std::endl;
        return data;
    } catch (const std::exception& e) {
        std::cerr << "Error loading account state: " << e.what() << std::endl;
        return json::object();
    }
}

bool PersistentStore::export_blockchain_state(const json& full_state) {
    try {
        std::string export_file = storage_dir_ + "/blockchain_export.json";
        std::ofstream f(export_file);
        f << full_state.dump(4);
        f.close();
        
        std::cerr << "Blockchain exported to: " << export_file << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error exporting blockchain: " << e.what() << std::endl;
        return false;
    }
}

json PersistentStore::import_blockchain_state() {
    std::string export_file = storage_dir_ + "/blockchain_export.json";
    
    if (!file_exists(export_file)) {
        // No blockchain export file found
        return json::object();
    }
    
    try {
        std::ifstream f(export_file);
        json data;
        f >> data;
        f.close();
        
        std::cerr << "Blockchain imported from export file" << std::endl;
        return data;
    } catch (const std::exception& e) {
        std::cerr << "Error importing blockchain: " << e.what() << std::endl;
        return json::object();
    }
}

void PersistentStore::clear_all_data() {
    try {
        std::remove(blocks_file_.c_str());
        std::remove(contracts_file_.c_str());
        std::remove(state_file_.c_str());
        std::cerr << "All data cleared from storage" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error clearing data: " << e.what() << std::endl;
    }
}

bool PersistentStore::has_saved_data() const {
    return file_exists(blocks_file_) || file_exists(contracts_file_) || 
           file_exists(state_file_);
}

int PersistentStore::get_block_count() {
    return load_blocks().size();
}

int PersistentStore::get_contract_count() {
    return load_contracts().size();
}

size_t PersistentStore::get_total_storage_size() {
    size_t total = 0;
    struct stat st = {};
    
    if (stat(blocks_file_.c_str(), &st) == 0) total += st.st_size;
    if (stat(contracts_file_.c_str(), &st) == 0) total += st.st_size;
    if (stat(state_file_.c_str(), &st) == 0) total += st.st_size;
    
    return total;
}
