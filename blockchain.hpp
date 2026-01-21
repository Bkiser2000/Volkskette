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

using json = nlohmann::json;

class BlockchainException : public std::exception {
private:
    std::string message;
public:
    BlockchainException(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
};

struct Transaction {
    std::string from;
    std::string to;
    double amount;
    std::string timestamp;

    json to_json() const {
        json j;
        j["from"] = from;
        j["to"] = to;
        j["amount"] = amount;
        j["timestamp"] = timestamp;
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

public:
    Blockchain();

    void add_transaction(const Transaction& tx);

    Block mine_block(int max_transactions = 10);

    Block get_previous_block() const;

    bool is_chain_valid() const;

    std::vector<Block> get_chain() const;

    json get_chain_json() const;

    void save_to_file(const std::string& filename) const;
    void load_from_file(const std::string& filename);

    size_t get_mempool_size() const;
};

#endif // BLOCKCHAIN_H
