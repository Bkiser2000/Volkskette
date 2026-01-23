#include "blockchain.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

// ============= KEY PAIR GENERATION =============
KeyPair KeyPair::generate() {
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) {
        throw BlockchainException("Failed to create EC key");
    }

    if (!EC_KEY_generate_key(ec_key)) {
        EC_KEY_free(ec_key);
        throw BlockchainException("Failed to generate key pair");
    }

    // Extract public key
    const EC_POINT* pub_key_point = EC_KEY_get0_public_key(ec_key);
    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    
    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    BN_CTX* ctx = BN_CTX_new();
    
    EC_POINT_get_affine_coordinates_GFp(group, pub_key_point, x, y, ctx);
    
    char* x_hex = BN_bn2hex(x);
    char* y_hex = BN_bn2hex(y);
    
    std::string public_key = std::string(x_hex) + std::string(y_hex);
    
    // Extract private key
    const BIGNUM* priv_key_bn = EC_KEY_get0_private_key(ec_key);
    char* priv_hex = BN_bn2hex(priv_key_bn);
    std::string private_key = std::string(priv_hex);
    
    // Cleanup
    OPENSSL_free(x_hex);
    OPENSSL_free(y_hex);
    OPENSSL_free(priv_hex);
    BN_free(x);
    BN_free(y);
    BN_CTX_free(ctx);
    EC_KEY_free(ec_key);

    return {public_key, private_key};
}

std::string KeyPair::public_key_to_address(const std::string& public_key) {
    // Simple address generation: hash of public key
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, public_key.c_str(), public_key.length());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return "0x" + ss.str().substr(0, 40);  // Similar to Ethereum format
}

// ============= TRANSACTION =============
std::string Transaction::calculate_hash() const {
    json j;
    j["from"] = from;
    j["to"] = to;
    j["amount"] = amount;
    j["gas_price"] = gas_price;
    j["timestamp"] = timestamp;
    j["public_key"] = public_key;
    
    // Hash without signature
    unsigned char hash[SHA256_DIGEST_LENGTH];
    std::string data = j.dump();
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.length());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// ============= SHA256 =============
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

// ============= MERKLE TREE =============
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

// ============= DIFFICULTY =============
int Blockchain::_calculate_difficulty() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.size() < 10) {
        return 4;
    }

    return 4 + (chain.size() / 100);
}

// ============= INITIALIZATION =============
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

// ============= SIGNATURE VERIFICATION =============
bool Blockchain::_verify_signature(const Transaction& tx) const {
    if (tx.signature.empty() || tx.public_key.empty()) {
        return false;
    }

    return tx.transaction_id == tx.calculate_hash();
}

// ============= ACCOUNT VALIDATION =============
bool Blockchain::_has_sufficient_balance(const std::string& address, double amount) const {
    auto it = account_balances.find(address);
    if (it == account_balances.end()) {
        return false;
    }
    return it->second >= amount;
}

void Blockchain::_update_balances(const std::vector<Transaction>& transactions) {
    for (const auto& tx : transactions) {
        account_balances[tx.from] -= (tx.amount + tx.gas_price);
        account_balances[tx.to] += tx.amount;
    }
}

// ============= TRANSACTION VALIDATION =============
bool Blockchain::_validate_transaction(const Transaction& tx) const {
    // 1. Verify signature
    if (!_verify_signature(tx)) {
        throw BlockchainException("Invalid transaction signature");
    }

    // 2. Verify sender has sufficient balance
    if (!_has_sufficient_balance(tx.from, tx.amount + tx.gas_price)) {
        throw BlockchainException("Insufficient balance for transaction");
    }

    // 3. Verify amounts are positive
    if (tx.amount <= 0 || tx.gas_price < 0) {
        throw BlockchainException("Invalid transaction amounts");
    }

    // 4. Verify addresses are valid
    if (tx.from.empty() || tx.to.empty()) {
        throw BlockchainException("Invalid transaction addresses");
    }

    // 5. Verify sender and receiver are different
    if (tx.from == tx.to) {
        throw BlockchainException("Sender and receiver cannot be the same");
    }

    return true;
}

// ============= ACCOUNT MANAGEMENT =============
void Blockchain::create_account(const std::string& address, double initial_balance) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (account_balances.find(address) != account_balances.end()) {
        throw BlockchainException("Account already exists");
    }
    
    account_balances[address] = initial_balance;
}

double Blockchain::get_balance(const std::string& address) const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    auto it = account_balances.find(address);
    if (it == account_balances.end()) {
        return 0.0;
    }
    return it->second;
}

std::map<std::string, double> Blockchain::get_all_balances() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return account_balances;
}

// ============= TRANSACTION CREATION =============
Transaction Blockchain::create_transaction(const std::string& from, const std::string& to,
                                          double amount, double gas_price,
                                          const std::string& private_key) {
    Transaction tx;
    tx.from = from;
    tx.to = to;
    tx.amount = amount;
    tx.gas_price = gas_price;
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buffer[100];
    std::tm* timeinfo = std::localtime(&time);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    tx.timestamp = std::string(buffer);

    // Calculate transaction hash
    tx.transaction_id = tx.calculate_hash();

    // Sign transaction (simplified - would use ECDSA in production)
    tx.signature = sha256(tx.transaction_id + private_key);

    return tx;
}

// ============= TRANSACTION POOL =============
void Blockchain::add_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (!_validate_transaction(tx)) {
        throw BlockchainException("Transaction validation failed");
    }
    
    mempool.push(tx);
}

// ============= POW & HASHING =============
std::string Blockchain::_to_digest(long long new_proof, long long previous_proof,
                                   int index, const std::string& data) const {
    long long calculation = (new_proof * new_proof) - (previous_proof * previous_proof) + index;
    return std::to_string(calculation) + data;
}

long long Blockchain::_proof_of_work(long long previous_proof, int index,
                                     const std::string& data, int diff) const {
    long long nonce = 0;
    bool check_proof = false;
    std::string target(diff, '0');

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

// ============= MINING =============
Block Blockchain::mine_block(int max_transactions) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.empty()) {
        throw BlockchainException("Chain is empty");
    }

    Block previous_block = chain.back();
    long long previous_proof = previous_block.proof;
    int index = chain.size() + 1;

    std::vector<Transaction> block_transactions;
    int tx_count = 0;
    while (!mempool.empty() && tx_count < max_transactions) {
        block_transactions.push_back(mempool.front());
        mempool.pop();
        tx_count++;
    }

    difficulty = _calculate_difficulty();

    std::string tx_data;
    for (const auto& tx : block_transactions) {
        tx_data += tx.to_json().dump();
    }

    long long proof = _proof_of_work(previous_proof, index, tx_data, difficulty);
    std::string previous_hash = _hash(previous_block);

    Block block = _create_block(block_transactions, proof, previous_hash, index);
    
    // Update balances after mining
    _update_balances(block_transactions);
    
    chain.push_back(block);

    return block;
}

// ============= VALIDATION =============
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

        std::string calculated_merkle = _calculate_merkle_root(block.transactions);
        if (block.merkle_root != calculated_merkle) {
            return false;
        }

        long long previous_proof = previous_block.proof;
        int index = block.index;
        
        std::string tx_data;
        for (const auto& tx : block.transactions) {
            tx_data += tx.to_json().dump();
        }
        
        long long proof = block.proof;

        std::string to_digest = _to_digest(proof, previous_proof, index, tx_data);
        std::string hash_operation = sha256(to_digest);

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

// ============= PERSISTENCE =============
void Blockchain::save_to_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw BlockchainException("Could not open file for writing: " + filename);
    }

    json j = json::object();
    j["chain"] = json::array();
    for (const auto& block : chain) {
        j["chain"].push_back(block.to_json());
    }
    
    j["balances"] = json::object();
    for (const auto& [address, balance] : account_balances) {
        j["balances"][address] = balance;
    }

    file << j.dump(2);
    file.close();
}

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
    account_balances.clear();
    
    for (const auto& block_json : j["chain"]) {
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
            tx.gas_price = tx_json["gas_price"];
            tx.timestamp = tx_json["timestamp"];
            tx.signature = tx_json["signature"];
            tx.public_key = tx_json["public_key"];
            tx.transaction_id = tx_json["transaction_id"];
            block.transactions.push_back(tx);
        }

        chain.push_back(block);
    }

    for (const auto& [address, balance] : j["balances"].items()) {
        account_balances[address] = balance;
    }
}

size_t Blockchain::get_mempool_size() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return mempool.size();
}
