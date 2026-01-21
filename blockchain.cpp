#include "blockchain.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>

std::string Blockchain::sha256(const std::string& str) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.length());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// 1. MERKLE TREE - Calculate merkle root from transactions
std::string Blockchain::_calculate_merkle_root(const std::vector<Transaction>& transactions) const {
    if (transactions.empty()) {
        return sha256("");
    }

    std::vector<std::string> hashes;
    for (const auto& tx : transactions) {
        hashes.push_back(sha256(tx.to_json().dump()));
    }

    while (hashes.size() > 1) {
        std::vector<std::string> next_level;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            std::string combined = hashes[i] + (i + 1 < hashes.size() ? hashes[i + 1] : hashes[i]);
            next_level.push_back(sha256(combined));
        }
        hashes = next_level;
    }

    return hashes[0];
}

// 5. DIFFICULTY ADJUSTMENT - Calculate dynamic difficulty
int Blockchain::_calculate_difficulty() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.size() < 10) {
        return 4;  // Default difficulty
    }

    // Adjust based on chain growth (simple implementation)
    return 4 + (chain.size() / 100);
}

Blockchain::Blockchain() : difficulty(4) {
    std::vector<Transaction> genesis_txs;
    Block genesis_block = _create_block(genesis_txs, 1, "0", 1);
    chain.push_back(genesis_block);
}

Block Blockchain::_create_block(const std::vector<Transaction>& transactions, 
                                long long proof,
                                const std::string& previous_hash, 
                                int index) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    char buffer[100];
    std::tm* timeinfo = std::localtime(&time);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    Block block;
    block.index = index;
    block.timestamp = std::string(buffer);
    block.transactions = transactions;
    block.merkle_root = _calculate_merkle_root(transactions);
    block.proof = proof;
    block.previous_hash = previous_hash;

    return block;
}

Block Blockchain::get_previous_block() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    if (chain.empty()) {
        throw BlockchainException("Chain is empty");
    }
    return chain.back();
}

std::string Blockchain::_to_digest(long long new_proof, long long previous_proof,
                                   int index, const std::string& data) const {
    long long calculation = (new_proof * new_proof) - (previous_proof * previous_proof) + index;
    return std::to_string(calculation) + data;
}

long long Blockchain::_proof_of_work(long long previous_proof, int index,
                                     const std::string& data, int diff) const {
    long long nonce = 0;
    bool check_proof = false;
    std::string target(diff, '0');  // Create string of leading zeros based on difficulty

    while (!check_proof) {
        std::string to_digest = _to_digest(nonce, previous_proof, index, data);
        std::string hash_operation = sha256(to_digest);

        if (hash_operation.substr(0, diff) == target) {
            check_proof = true;
        } else {
            nonce++;
        }
    }

    return nonce;
}

std::string Blockchain::_hash(const Block& block) const {
    json j = block.to_json();
    std::string encoded_block = j.dump();
    return sha256(encoded_block);
}

// 4. TRANSACTION POOL - Add transaction to mempool
void Blockchain::add_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    mempool.push(tx);
}

// 4. MINE BLOCK - Updated to use transactions from mempool
Block Blockchain::mine_block(int max_transactions) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.empty()) {
        throw BlockchainException("Chain is empty");
    }

    Block previous_block = chain.back();
    long long previous_proof = previous_block.proof;
    int index = chain.size() + 1;

    // Pull transactions from mempool
    std::vector<Transaction> block_transactions;
    int tx_count = 0;
    while (!mempool.empty() && tx_count < max_transactions) {
        block_transactions.push_back(mempool.front());
        mempool.pop();
        tx_count++;
    }

    // Calculate difficulty
    difficulty = _calculate_difficulty();

    // Create digest from transactions
    std::string tx_data;
    for (const auto& tx : block_transactions) {
        tx_data += tx.to_json().dump();
    }

    long long proof = _proof_of_work(previous_proof, index, tx_data, difficulty);
    std::string previous_hash = _hash(previous_block);

    Block block = _create_block(block_transactions, proof, previous_hash, index);
    chain.push_back(block);

    return block;
}

// 3. THREAD SAFETY - Added mutex locking
bool Blockchain::is_chain_valid() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.empty()) return false;

    Block previous_block = chain[0];
    size_t block_index = 1;

    while (block_index < chain.size()) {
        Block block = chain[block_index];

        if (block.previous_hash != _hash(previous_block)) {
            return false;
        }

        // Verify merkle root
        std::string calculated_merkle = _calculate_merkle_root(block.transactions);
        if (block.merkle_root != calculated_merkle) {
            return false;
        }

        long long previous_proof = previous_block.proof;
        int index = block.index;
        
        // Create digest from all transactions
        std::string tx_data;
        for (const auto& tx : block.transactions) {
            tx_data += tx.to_json().dump();
        }
        
        long long proof = block.proof;

        std::string to_digest = _to_digest(proof, previous_proof, index, tx_data);
        std::string hash_operation = sha256(to_digest);

        // Check if hash has correct number of leading zeros
        std::string target(4, '0');
        if (hash_operation.substr(0, 4) != target) {
            return false;
        }

        previous_block = block;
        block_index++;
    }

    return true;
}

std::vector<Block> Blockchain::get_chain() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return chain;
}

json Blockchain::get_chain_json() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    json j = json::array();
    for (const auto& block : chain) {
        j.push_back(block.to_json());
    }
    return j;
}

// 6. PERSISTENCE - Save blockchain to file
void Blockchain::save_to_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw BlockchainException("Could not open file for writing: " + filename);
    }

    json j = json::array();
    for (const auto& block : chain) {
        j.push_back(block.to_json());
    }

    file << j.dump(2);
    file.close();
}

// 6. PERSISTENCE - Load blockchain from file
void Blockchain::load_from_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw BlockchainException("Could not open file for reading: " + filename);
    }

    json j;
    file >> j;
    file.close();

    chain.clear();
    
    for (const auto& block_json : j) {
        Block block;
        block.index = block_json["index"];
        block.timestamp = block_json["timestamp"];
        block.merkle_root = block_json["merkle_root"];
        block.proof = block_json["proof"];
        block.previous_hash = block_json["previous_hash"];

        for (const auto& tx_json : block_json["transactions"]) {
            Transaction tx;
            tx.from = tx_json["from"];
            tx.to = tx_json["to"];
            tx.amount = tx_json["amount"];
            tx.timestamp = tx_json["timestamp"];
            block.transactions.push_back(tx);
        }

        chain.push_back(block);
    }
}

size_t Blockchain::get_mempool_size() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return mempool.size();
}
