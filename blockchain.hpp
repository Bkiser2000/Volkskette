#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <vector>
#include <string>
#include <ctime>
#include <queue>
#include <mutex>
#include <fstream>
#include <stdexcept>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <map>
#include "contract.hpp"
#include "persistent_store.hpp"
#include "utils/logger.hpp"

using json = nlohmann::json;

class BlockchainException : public std::exception {
private:
    std::string message;
public:
    BlockchainException(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
};

struct KeyPair {
    std::string public_key;
    std::string private_key;

    static KeyPair generate();
    static std::string public_key_to_address(const std::string& public_key);
};

struct Transaction {
    std::string from;
    std::string to;
    double amount;
    double gas_price;
    std::string timestamp;
    std::string signature;
    std::string public_key;
    std::string transaction_id;
    uint64_t nonce = 0;  // Replay attack protection
    std::string data;    // Optional transaction data
    std::string contract_address;  // If calling a contract
    bool is_contract_deployment = false;  // Deploy new contract
    std::string contract_bytecode;  // For deployments
    std::string contract_name;      // For deployments
    std::string contract_language;  // For deployments

    json to_json() const {
        json j;
        j["from"] = from;
        j["to"] = to;
        j["amount"] = amount;
        j["gas_price"] = gas_price;
        j["timestamp"] = timestamp;
        j["signature"] = signature;
        j["public_key"] = public_key;
        j["transaction_id"] = transaction_id;
        j["nonce"] = nonce;
        j["data"] = data;
        j["contract_address"] = contract_address;
        j["is_contract_deployment"] = is_contract_deployment;
        j["contract_bytecode"] = contract_bytecode;
        j["contract_name"] = contract_name;
        j["contract_language"] = contract_language;
        return j;
    }
    std::string calculate_hash() const;
};

const double BLOCK_REWARD = 50.0;
const double GAS_REWARD_PERCENTAGE = 0.9;

struct MinerStats {
    std::string address;
    long long blocks_mined = 0;
    double total_rewards = 0.0;

    json to_json() const {
        json j;
        j["address"] = address;
        j["blocks_mined"] = blocks_mined;
        j["total_rewards"] = total_rewards;
        return j;
    }
};

struct Block {
    int index;
    std::string timestamp;
    std::vector<Transaction> transactions;
    std::string merkle_root;
    long long proof;
    std::string previous_hash;

    json to_json() const {
        json j;
        j["index"] = index;
        j["timestamp"] = timestamp;
        j["transactions"] = json::array();
        for (const auto& tx : transactions) {
            j["transactions"].push_back(tx.to_json());
        }
        j["merkle_root"] = merkle_root;
        j["proof"] = proof;
        j["previous_hash"] = previous_hash;
        return j;
    }
};

class Blockchain {
private:
    // Blockchain state
    std::vector<Block> chain;
    mutable std::mutex chain_mutex;
    int difficulty;

    // Memory-managed transaction pool with capacity limits
    std::queue<Transaction> mempool;
    mutable std::mutex mempool_mutex;
    static constexpr size_t MAX_MEMPOOL_SIZE = 10000;  // Max transactions in mempool
    static constexpr size_t MEMPOOL_EVICT_SIZE = 1000; // Evict this many when full

    // Account state with efficient storage
    std::map<std::string, double> account_balances;
    std::map<std::string, uint64_t> account_nonces;  // Track nonce per account
    std::map<std::string, MinerStats> miner_stats;
    
    ContractManager contract_manager_;  // Smart contract management
    ContractVM contract_vm_;            // Contract execution engine
    PersistentStore persistent_store_;  // Blockchain state persistence

    Block _create_block(const std::vector<Transaction>& transactions, 
                       long long proof,
                       const std::string& previous_hash, 
                       int index);

    std::string _to_digest(long long new_proof, long long previous_proof,
                          int index, const std::string& data) const;

    long long _proof_of_work(long long previous_proof, int index,
                            const std::string& data, int difficulty) const;

    std::string _hash(const Block& block) const;

    std::string sha256(const std::string& str) const;

    std::string _calculate_merkle_root(const std::vector<Transaction>& transactions) const;

    int _calculate_difficulty() const;

    bool _verify_signature(const Transaction& tx) const;

    bool _verify_ecdsa_signature(const Transaction& tx) const;

    bool _validate_transaction(const Transaction& tx) const;

    bool _has_sufficient_balance(const std::string& address, double amount) const;

    bool _check_replay_protection(const Transaction& tx) const;

    void _update_balances(const std::vector<Transaction>& transactions);

public:
    Blockchain();

    static constexpr double INITIAL_BALANCE = 100.0;

    void create_account(const std::string& address, double initial_balance = INITIAL_BALANCE);

    double get_balance(const std::string& address) const;

    std::map<std::string, double> get_all_balances() const;

    uint64_t get_account_nonce(const std::string& address) const;

    void add_transaction(const Transaction& tx);

    Transaction create_transaction(const std::string& from,
                                  const std::string& to,
                                  double amount,
                                  double gas_price,
                                  const std::string& private_key);

    Transaction create_transaction_with_nonce(const std::string& from,
                                             const std::string& to,
                                             double amount,
                                             double gas_price,
                                             uint64_t nonce,
                                             const std::string& private_key);

    Transaction create_coinbase_transaction(const std::string& miner_address, int block_index);

    void record_miner_reward(const std::string& miner_address, double reward);

    double get_miner_total_rewards(const std::string& miner_address) const;

    Block mine_block(int max_transactions = 10);

    Block get_previous_block() const;

    bool is_chain_valid() const;

    std::vector<Block> get_chain() const;

    std::map<std::string, MinerStats> get_all_miner_stats() const;

    json get_chain_json() const;

    void save_to_file(const std::string& filename) const;
    void load_from_file(const std::string& filename);

    size_t get_mempool_size() const;
    
    // Contract management
    ContractManager& get_contract_manager() { return contract_manager_; }
    const ContractManager& get_contract_manager() const { return contract_manager_; }
    ContractVM& get_contract_vm() { return contract_vm_; }
    
    // Contract operations
    std::string deploy_contract(const std::string& creator, const std::string& name,
                               const std::string& language, const std::vector<uint8_t>& bytecode);
    bool call_contract(const std::string& contract_address, const std::string& caller,
                      const std::string& method, const std::vector<std::string>& params);
    SmartContract* get_contract(const std::string& address);
    
    // Persistence
    PersistentStore& get_persistent_store() { return persistent_store_; }
    bool save_blockchain_state();
    bool load_blockchain_state();
};

#endif // BLOCKCHAIN_H
