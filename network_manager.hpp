#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include "node.hpp"
#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <thread>

/**
 * NetworkManager - Coordinates multiple blockchain nodes
 * Handles peer discovery, synchronization, and consensus
 */
class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Node management (returns non-null pointers or nullptr)
    BlockchainNode* create_node(const std::string& node_id, uint16_t port, int difficulty = 4);
    BlockchainNode* get_node(const std::string& node_id) const;
    std::vector<BlockchainNode*> get_all_nodes() const;
    
    // Safe node access with validation
    bool has_node(const std::string& node_id) const;
    
    // Network topology
    void connect_peers(const std::string& node_id1, const std::string& node_id2);
    void start_all_nodes();
    void stop_all_nodes();
    
    // Consensus monitoring
    void monitor_consensus();
    bool is_network_synced(int max_height_diff = 0) const;
    void wait_for_sync(int timeout_seconds = 30);
    
    // State synchronization (Phase 4.2)
    bool is_state_synced() const;
    std::map<std::string, std::string> get_state_roots() const;
    
    // Network statistics
    int get_network_height() const;
    std::map<std::string, int> get_chain_heights() const;
    std::map<std::string, bool> get_sync_status() const;
    
private:
    std::map<std::string, std::unique_ptr<BlockchainNode>> nodes_;
    mutable std::mutex nodes_mutex_;
    
    std::thread consensus_thread_;
    bool running_ = false;
    
    // Consensus helper
    void run_consensus_monitor();
    void sync_chains();
    void sync_states();  // Phase 4.2: Synchronize account states
    std::vector<Block> resolve_fork(const std::vector<std::vector<Block>>& competing_chains);
};

#endif // NETWORK_MANAGER_HPP
