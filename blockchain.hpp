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
    std::vector<Block> chain;
    std::queue<Transaction> mempool;
    mutable std::mutex chain_mutex;
    int difficulty;

    std::map<std::string, double> account_balances;

    std::map<std::string, MinerStats> miner_stats;

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

    bool _validate_transaction(const Transaction& tx) const;

    bool _has_sufficient_balance(const std::string& address, double amount) const;

    void _update_balances(const std::vector<Transaction>& transactions);

public:
    Blockchain();

    static constexpr double INITIAL_BALANCE = 100.0;

    void create_account(const std::string& address, double initial_balance = INITIAL_BALANCE);

    double get_balance(const std::string& address) const;

    std::map<std::string, double> get_all_balances() const;

    void add_transaction(const Transaction& tx);

    Transaction create_transaction(const std::string& from,
                                  const std::string& to,
                                  double amount,
                                  double gas_price,
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
};

#endif // BLOCKCHAIN_H
