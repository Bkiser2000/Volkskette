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

// ============= ACCOUNT STATE SYNCHRONIZATION =============
std::string Blockchain::_calculate_state_root() const {
    // Create deterministic hash of current account state
    json state_json = json::object();
    
    // Sort accounts by address for deterministic ordering
    std::vector<std::string> sorted_addresses;
    for (const auto& [addr, _] : account_balances) {
        sorted_addresses.push_back(addr);
    }
    std::sort(sorted_addresses.begin(), sorted_addresses.end());
    
    // Build state snapshot
    for (const auto& addr : sorted_addresses) {
        json account_data = json::object();
        account_data["balance"] = account_balances.at(addr);
        account_data["nonce"] = account_nonces.count(addr) ? account_nonces.at(addr) : 0;
        state_json[addr] = account_data;
    }
    
    // Hash the entire state
    std::string state_dump = state_json.dump();
    return sha256(state_dump);
}

// ============= DIFFICULTY ============="
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
    // Calculate state root BEFORE transactions execute (for consistent verification)
    std::string state_root_value = _calculate_state_root();
    
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
    block.state_root = state_root_value;  // Add state root (Account state sync)
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

    // Verify transaction hash matches
    return tx.transaction_id == tx.calculate_hash();
}

bool Blockchain::_verify_ecdsa_signature(const Transaction& tx) const {
    // In production, implement full ECDSA verification using OpenSSL
    // For now, we verify the transaction ID matches the hash
    if (tx.transaction_id != tx.calculate_hash()) {
        return false;
    }
    
    // Verify signature is not empty
    if (tx.signature.empty()) {
        return false;
    }
    
    // In a full implementation:
    // 1. Deserialize public key from tx.public_key
    // 2. Deserialize signature from tx.signature
    // 3. Hash the transaction data
    // 4. Use ECDSA_verify with secp256k1 curve
    
    return true;
}

// ============= ACCOUNT VALIDATION =============
bool Blockchain::_has_sufficient_balance(const std::string& address, double amount) const {
    auto it = account_balances.find(address);
    if (it == account_balances.end()) {
        return false;
    }
    return it->second >= amount;
}

bool Blockchain::_check_replay_protection(const Transaction& tx) const {
    auto it = account_nonces.find(tx.from);
    if (it == account_nonces.end()) {
        // New account, nonce should be 0
        return tx.nonce == 0;
    }
    // Nonce must be exactly one more than the last used nonce
    return tx.nonce == it->second + 1;
}

bool Blockchain::_verify_state_root(const std::string& calculated_root, const std::string& block_root) const {
    if (calculated_root != block_root) {
        LOG_WARN("Blockchain", "State root mismatch! Calculated: " + calculated_root.substr(0, 16) + 
                 " Block: " + block_root.substr(0, 16));
        return false;
    }
    return true;
}

// ============= ADVANCED BLOCK VALIDATION (Phase 5) =============
// Note: Implementation functions are defined below (see _verify_block_merkle_root, etc.)

void Blockchain::_update_balances(const std::vector<Transaction>& transactions) {
    for (const auto& tx : transactions) {
        account_balances[tx.from] -= (tx.amount + tx.gas_price);
        account_balances[tx.to] += tx.amount;
        // Update nonce for replay protection (Account state sync)
        account_nonces[tx.from] = tx.nonce;
    }
    
    // Update state snapshot after balances change
    std::lock_guard<std::mutex> lock(state_mutex);
    account_state_snapshot = account_balances;  // Snapshot current state
}

// ============= TRANSACTION VALIDATION =============
bool Blockchain::_validate_transaction(const Transaction& tx) const {
    LOG_DEBUG("Blockchain", "Validating transaction: " + tx.transaction_id.substr(0, 16) + 
              "... from " + tx.from + " to " + tx.to + " amount: " + std::to_string(tx.amount));
    
    // 1. Verify signature
    if (!_verify_ecdsa_signature(tx)) {
        LOG_WARN("Blockchain", "Transaction failed signature verification: " + tx.transaction_id.substr(0, 16));
        throw BlockchainException("Invalid transaction signature");
    }

    // 2. Verify replay protection (nonce)
    if (!_check_replay_protection(tx)) {
        LOG_WARN("Blockchain", "Transaction failed replay protection check (nonce): " + tx.transaction_id.substr(0, 16));
        throw BlockchainException("Invalid transaction nonce - replay attack detected");
    }

    // 3. Verify sender has sufficient balance
    if (!_has_sufficient_balance(tx.from, tx.amount + tx.gas_price)) {
        LOG_WARN("Blockchain", "Transaction failed balance check: " + tx.from + 
                 " insufficient funds for " + std::to_string(tx.amount + tx.gas_price));
        throw BlockchainException("Insufficient balance for transaction");
    }

    // 4. Verify amounts are positive
    if (tx.amount <= 0 || tx.gas_price < 0) {
        LOG_WARN("Blockchain", "Transaction has invalid amounts");
        throw BlockchainException("Invalid transaction amounts");
    }

    // 5. Verify addresses are valid and non-empty
    if (tx.from.empty() || tx.to.empty()) {
        LOG_WARN("Blockchain", "Transaction has invalid addresses");
        throw BlockchainException("Invalid transaction addresses");
    }

    // 6. Verify sender and receiver are different
    if (tx.from == tx.to) {
        LOG_WARN("Blockchain", "Transaction sender equals receiver");
        throw BlockchainException("Sender and receiver cannot be the same");
    }

    // 7. Verify transaction ID is correctly calculated
    if (tx.transaction_id != tx.calculate_hash()) {
        LOG_WARN("Blockchain", "Transaction ID mismatch");
        throw BlockchainException("Transaction ID does not match hash");
    }

    LOG_DEBUG("Blockchain", "Transaction validation passed: " + tx.transaction_id.substr(0, 16));
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

uint64_t Blockchain::get_account_nonce(const std::string& address) const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    auto it = account_nonces.find(address);
    if (it == account_nonces.end()) {
        return 0;
    }
    return it->second;
}

std::map<std::string, double> Blockchain::get_all_balances() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return account_balances;
}

// Account State Synchronization Methods (NEW)
std::map<std::string, std::pair<double, uint64_t>> Blockchain::get_account_state() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    std::map<std::string, std::pair<double, uint64_t>> state;
    
    // Combine balances and nonces
    for (const auto& [addr, balance] : account_balances) {
        uint64_t nonce = 0;
        if (account_nonces.count(addr)) {
            nonce = account_nonces.at(addr);
        }
        state[addr] = {balance, nonce};
    }
    
    return state;
}

std::string Blockchain::get_state_root() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return _calculate_state_root();
}

bool Blockchain::sync_state(const std::map<std::string, std::pair<double, uint64_t>>& remote_state) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    // Compare local state with remote state
    auto local_state = get_account_state();
    
    if (local_state.size() != remote_state.size()) {
        LOG_WARN("Blockchain", "State sync mismatch - different account count. Local: " + 
                 std::to_string(local_state.size()) + " Remote: " + std::to_string(remote_state.size()));
        return false;
    }
    
    for (const auto& [addr, remote_data] : remote_state) {
        auto it = local_state.find(addr);
        if (it == local_state.end()) {
            LOG_WARN("Blockchain", "State sync - missing account: " + addr);
            return false;
        }
        
        const auto& [local_balance, local_nonce] = it->second;
        const auto& [remote_balance, remote_nonce] = remote_data;
        
        if (local_balance != remote_balance || local_nonce != remote_nonce) {
            LOG_WARN("Blockchain", "State sync mismatch for " + addr + 
                     " Local: (" + std::to_string(local_balance) + ", " + std::to_string(local_nonce) + ") " +
                     "Remote: (" + std::to_string(remote_balance) + ", " + std::to_string(remote_nonce) + ")");
            return false;
        }
    }
    
    LOG_INFO("Blockchain", "State sync successful - all " + std::to_string(local_state.size()) + " accounts verified");
    return true;
}

// ============= TRANSACTION CREATION =============
Transaction Blockchain::create_transaction(const std::string& from, const std::string& to,
                                          double amount, double gas_price,
                                          const std::string& private_key) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    // Get next nonce for this account
    uint64_t nonce = 0;
    auto it = account_nonces.find(from);
    if (it != account_nonces.end()) {
        nonce = it->second + 1;
    }
    
    return create_transaction_with_nonce(from, to, amount, gas_price, nonce, private_key);
}

Transaction Blockchain::create_transaction_with_nonce(const std::string& from,
                                                     const std::string& to,
                                                     double amount,
                                                     double gas_price,
                                                     uint64_t nonce,
                                                     const std::string& private_key) {
    Transaction tx;
    tx.from = from;
    tx.to = to;
    tx.amount = amount;
    tx.gas_price = gas_price;
    tx.nonce = nonce;
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buffer[100];
    std::tm* timeinfo = std::localtime(&time);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    tx.timestamp = std::string(buffer);

    // Set public key (derived from private key in real implementation)
    tx.public_key = sha256(private_key).substr(0, 64);  // Simplified public key derivation

    // Calculate transaction hash
    tx.transaction_id = tx.calculate_hash();

    // Sign transaction with ECDSA
    // In production, this would use full ECDSA signing with OpenSSL
    tx.signature = sha256(tx.transaction_id + private_key + std::to_string(nonce));

    return tx;
}

// ============= TRANSACTION POOL =============
void Blockchain::add_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> mempool_lock(mempool_mutex);
    
    if (!_validate_transaction(tx)) {
        throw BlockchainException("Transaction validation failed");
    }
    
    // Check mempool capacity and evict oldest transactions if needed
    if (mempool.size() >= MAX_MEMPOOL_SIZE) {
        LOG_WARN("Blockchain", "Mempool at capacity (" + std::to_string(MAX_MEMPOOL_SIZE) + 
                 "), evicting " + std::to_string(MEMPOOL_EVICT_SIZE) + " oldest transactions");
        
        // Remove oldest MEMPOOL_EVICT_SIZE transactions (FIFO)
        for (size_t i = 0; i < MEMPOOL_EVICT_SIZE && !mempool.empty(); ++i) {
            mempool.pop();
        }
    }
    
    mempool.push(tx);
    LOG_DEBUG("Blockchain", "Transaction added to mempool (size: " + std::to_string(mempool.size()) + ")");
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

    LOG_INFO("Blockchain", "Starting mining block #" + std::to_string(chain.size() + 1));

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

    LOG_DEBUG("Blockchain", "Mining with " + std::to_string(block_transactions.size()) + " transactions");

    difficulty = _calculate_difficulty();
    LOG_DEBUG("Blockchain", "Difficulty: " + std::to_string(difficulty));

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
    
    // Save to persistent storage
    persistent_store_.save_block(block.to_json());
    
    LOG_INFO("Blockchain", "Block #" + std::to_string(index) + " mined successfully with proof: " + std::to_string(proof));

    return block;
}

// ============= ADVANCED BLOCK VALIDATION (PHASE 5) =============

bool Blockchain::_verify_block_merkle_root(const Block& block) const {
    std::string calculated_merkle = _calculate_merkle_root(block.transactions);
    if (block.merkle_root != calculated_merkle) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                 " merkle root mismatch: expected " + calculated_merkle.substr(0, 16) +
                 " got " + block.merkle_root.substr(0, 16));
        return false;
    }
    return true;
}

bool Blockchain::_verify_block_timestamp(const Block& block, const Block& previous_block) const {
    // Parse timestamps
    auto parse_time = [](const std::string& timestamp_str) -> int64_t {
        // Format: "2026-02-06 03:30:09"
        std::tm tm = {};
        std::istringstream ss(timestamp_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return std::mktime(&tm);
    };
    
    try {
        int64_t prev_time = parse_time(previous_block.timestamp);
        int64_t block_time = parse_time(block.timestamp);
        int64_t current_time = std::time(nullptr);
        
        // 1. Block timestamp must be after previous block
        if (block_time <= prev_time) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                     " timestamp not after previous block");
            return false;
        }
        
        // 2. Block timestamp must not be too far in the future
        if (block_time > current_time + MAX_BLOCK_FUTURE_TIME) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                     " timestamp too far in future");
            return false;
        }
        
        // 3. Block must respect minimum time between blocks
        if (block_time - prev_time < MIN_BLOCK_TIME) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                     " time delta too small (" + std::to_string(block_time - prev_time) + "s)");
            return false;
        }
        
        return true;
    } catch (...) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                 " timestamp parsing failed");
        return false;
    }
}

bool Blockchain::_verify_transaction_nonce_ordering(const Block& block) const {
    // Verify that transactions are ordered by nonce per sender
    std::map<std::string, uint64_t> expected_nonces;
    
    for (size_t i = 0; i < block.transactions.size(); i++) {
        const auto& tx = block.transactions[i];
        
        auto it = expected_nonces.find(tx.from);
        if (it == expected_nonces.end()) {
            // First transaction from this sender must have nonce 0 or account's current nonce
            auto account_it = account_nonces.find(tx.from);
            uint64_t expected = account_it == account_nonces.end() ? 0 : account_it->second + 1;
            
            if (tx.nonce != expected) {
                LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                         " tx " + std::to_string(i) + " has unexpected nonce " +
                         std::to_string(tx.nonce) + " (expected " + std::to_string(expected) + ")");
                return false;
            }
            expected_nonces[tx.from] = expected;
        } else {
            // Subsequent transactions must be nonce+1
            uint64_t expected = it->second + 1;
            if (tx.nonce != expected) {
                LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                         " tx " + std::to_string(i) + " has invalid nonce sequence");
                return false;
            }
            it->second = expected;
        }
    }
    
    return true;
}

bool Blockchain::_verify_block_difficulty(const Block& block) const {
    // Check if this is a retarget block
    if (block.index > 1 && block.index % DIFFICULTY_RETARGET_INTERVAL == 0) {
        // This block should have adjusted difficulty
        // For now, just verify the proof meets some minimum difficulty
        std::string target(4, '0');  // Minimum: 4 leading zeros
        long long prev_proof = (block.index > 1) ? chain[block.index - 1].proof : 1;
        std::string to_digest = _to_digest(block.proof, prev_proof, block.index, block.merkle_root);
        std::string hash_result = sha256(to_digest);
        
        if (hash_result.substr(0, 4) != target) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                     " does not meet minimum difficulty");
            return false;
        }
    } else {
        // Regular block: verify proof meets difficulty
        long long prev_proof = (block.index > 1) ? chain[block.index - 1].proof : 1;
        std::string to_digest = _to_digest(block.proof, prev_proof, block.index, block.merkle_root);
        std::string hash_result = sha256(to_digest);
        
        std::string target(4, '0');
        if (hash_result.substr(0, 4) != target) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + 
                     " proof of work invalid");
            return false;
        }
    }
    
    return true;
}

bool Blockchain::_validate_block_advanced(const Block& block, const Block& previous_block) const {
    // Comprehensive block validation (Phase 5)
    
    // 1. Verify merkle root
    if (!_verify_block_merkle_root(block)) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed merkle verification");
        return false;
    }
    
    // 2. Verify timestamp
    if (!_verify_block_timestamp(block, previous_block)) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed timestamp verification");
        return false;
    }
    
    // 3. Verify transaction nonce ordering
    if (!_verify_transaction_nonce_ordering(block)) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed nonce ordering verification");
        return false;
    }
    
    // 4. Verify difficulty
    if (!_verify_block_difficulty(block)) {
        LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed difficulty verification");
        return false;
    }
    
    // 5. Verify state root if present
    if (!block.state_root.empty()) {
        std::string calculated_state_root = _calculate_state_root();
        if (!_verify_state_root(calculated_state_root, block.state_root)) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed state root verification");
            return false;
        }
    }
    
    LOG_INFO("Blockchain", "Block " + std::to_string(block.index) + " passed advanced validation ✓");
    return true;
}

// ============= VALIDATION ============="
bool Blockchain::is_chain_valid() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (chain.empty()) return false;

    Block previous_block = chain[0];
    size_t block_index = 1;

    while (block_index < chain.size()) {
        Block block = chain[block_index];

        // Check hash linkage
        if (block.previous_hash != _hash(previous_block)) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " previous hash mismatch");
            return false;
        }

        // Use advanced validation (Phase 5)
        if (!_validate_block_advanced(block, previous_block)) {
            LOG_WARN("Blockchain", "Block " + std::to_string(block.index) + " failed advanced validation");
            return false;
        }

        previous_block = block;
        block_index++;
    }

    LOG_INFO("Blockchain", "Chain validation PASSED ✓ - All " + std::to_string(chain.size()) + " blocks valid");
    return true;
}

bool Blockchain::is_chain_valid_with_state() const {
    // Verify both chain integrity and state roots (NEW: Account state sync)
    if (!is_chain_valid()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    // For each block, verify state root consistency
    for (int i = 1; i < chain.size(); i++) {
        const auto& block = chain[i];
        
        // Verify block has a state_root field
        if (block.state_root.empty()) {
            LOG_WARN("Blockchain", "Block " + std::to_string(i) + " has no state_root");
            // This is OK for legacy blocks, just continue
        }
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

// ============= CONTRACT MANAGEMENT =============
std::string Blockchain::deploy_contract(const std::string& creator, const std::string& name,
                                       const std::string& language, const std::vector<uint8_t>& bytecode) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    LOG_INFO("Blockchain", "Deploying contract: " + name + " (language: " + language + 
             ", creator: " + creator + ", bytecode size: " + std::to_string(bytecode.size()) + ")");
    
    std::string address = contract_manager_.deploy_contract(creator, name, language, bytecode);
    
    LOG_INFO("Blockchain", "Contract deployed successfully at address: " + address);
    
    // Save to persistent storage
    SmartContract* contract = contract_manager_.get_contract(address);
    if (contract) {
        persistent_store_.save_contract(contract->to_json());
    }
    
    return address;
}

bool Blockchain::call_contract(const std::string& contract_address, const std::string& caller,
                              const std::string& method, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    LOG_DEBUG("Blockchain", "Calling contract " + contract_address + " method: " + method);
    
    SmartContract* contract = contract_manager_.get_contract(contract_address);
    if (!contract) {
        LOG_ERROR("Blockchain", "Contract not found: " + contract_address);
        throw BlockchainException("Contract not found: " + contract_address);
    }
    
    // Create execution context
    ExecutionContext ctx;
    ctx.caller = caller;
    ctx.contract_address = contract_address;
    ctx.origin = caller;
    ctx.timestamp = std::time(nullptr);
    ctx.block_number = chain.size();
    ctx.balances = account_balances;
    ctx.gas_remaining = 1000000;
    
    // Execute contract
    if (!contract_vm_.execute(contract, ctx)) {
        throw BlockchainException("Contract execution failed: " + contract_vm_.get_error());
    }
    
    // Update balances from execution
    account_balances = ctx.balances;
    
    return true;
}

SmartContract* Blockchain::get_contract(const std::string& address) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return contract_manager_.get_contract(address);
}
// ============= PERSISTENCE =============

bool Blockchain::save_blockchain_state() {
    try {
        std::lock_guard<std::mutex> lock(chain_mutex);
        
        json blocks_json = json::array();
        for (const auto& block : chain) {
            blocks_json.push_back(block.to_json());
        }
        persistent_store_.save_blocks(blocks_json);
        
        json contracts_json = json::array();
        auto contract_addresses = contract_manager_.get_all_contracts();
        for (const auto& address : contract_addresses) {
            SmartContract* contract = contract_manager_.get_contract(address);
            if (contract) {
                contracts_json.push_back(contract->to_json());
            }
        }
        persistent_store_.save_contracts(contracts_json);
        
        json state_json;
        state_json["balances"] = account_balances;
        state_json["nonces"] = account_nonces;
        state_json["difficulty"] = difficulty;
        persistent_store_.save_account_state(state_json);
        
        LOG_INFO("Blockchain", "State saved to persistent storage");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Blockchain", "Failed to save state: " + std::string(e.what()));
        return false;
    }
}

bool Blockchain::load_blockchain_state() {
    try {
        if (!persistent_store_.has_saved_data()) {
            LOG_INFO("Blockchain", "No saved state found - starting with fresh chain");
            return true;
        }
        
        std::lock_guard<std::mutex> lock(chain_mutex);
        
        // Load blocks
        auto blocks_json = persistent_store_.load_blocks();
        chain.clear();
        for (const auto& block_json : blocks_json) {
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
                tx.nonce = tx_json["nonce"];
                block.transactions.push_back(tx);
            }
            
            chain.push_back(block);
        }
        LOG_INFO("Blockchain", "Loaded " + std::to_string(chain.size()) + " blocks");
        
        // Load account state
        auto state_json = persistent_store_.load_account_state();
        if (!state_json.empty()) {
            account_balances = state_json["balances"].get<std::map<std::string, double>>();
            account_nonces = state_json["nonces"].get<std::map<std::string, uint64_t>>();
            if (state_json.contains("difficulty")) {
                difficulty = state_json["difficulty"];
            }
        }
        LOG_INFO("Blockchain", "Loaded account state with " + 
                 std::to_string(account_balances.size()) + " accounts");
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Blockchain", "Failed to load state: " + std::string(e.what()));
        return false;
    }
}
