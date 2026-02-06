#include "network_manager.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

NetworkManager::NetworkManager() {
    LOG_INFO("NetworkManager", "Initializing network manager");
}

NetworkManager::~NetworkManager() {
    stop_all_nodes();
    LOG_INFO("NetworkManager", "Network manager shutting down");
}

BlockchainNode* NetworkManager::create_node(const std::string& node_id, uint16_t port, int difficulty) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto node = std::make_unique<BlockchainNode>(node_id, port, difficulty);
    BlockchainNode* node_ptr = node.get();
    nodes_[node_id] = std::move(node);
    
    LOG_INFO("NetworkManager", "Created node: " + node_id + " on port " + std::to_string(port));
    return node_ptr;
}

BlockchainNode* NetworkManager::get_node(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<BlockchainNode*> NetworkManager::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<BlockchainNode*> result;
    for (const auto& [_, node] : nodes_) {
        result.push_back(node.get());
    }
    return result;
}

bool NetworkManager::has_node(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    return nodes_.find(node_id) != nodes_.end();
}

void NetworkManager::connect_peers(const std::string& node_id1, const std::string& node_id2) {
    auto node1 = get_node(node_id1);
    auto node2 = get_node(node_id2);
    
    if (!node1 || !node2) {
        LOG_ERROR("NetworkManager", "Cannot connect peers: one or both nodes not found");
        return;
    }
    
    // Register peers with each other
    node1->add_peer(node_id2, "localhost:" + std::to_string(node2->get_port()));
    node2->add_peer(node_id1, "localhost:" + std::to_string(node1->get_port()));
    
    LOG_INFO("NetworkManager", "Connected peers: " + node_id1 + " <-> " + node_id2);
    std::cout << "✓ Connected " << node_id1 << " <-> " << node_id2 << std::endl;
}

void NetworkManager::start_all_nodes() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    LOG_INFO("NetworkManager", "Starting all nodes");
    for (auto& [node_id, node] : nodes_) {
        try {
            node->start();
            LOG_INFO("NetworkManager", "Started node: " + node_id);
        } catch (const std::exception& e) {
            LOG_ERROR("NetworkManager", "Failed to start node " + node_id + ": " + e.what());
        }
    }
    
    running_ = true;
    consensus_thread_ = std::thread(&NetworkManager::run_consensus_monitor, this);
}

void NetworkManager::stop_all_nodes() {
    running_ = false;
    
    if (consensus_thread_.joinable()) {
        consensus_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    LOG_INFO("NetworkManager", "Stopping all nodes");
    
    for (auto& [node_id, node] : nodes_) {
        try {
            node->stop();
            LOG_INFO("NetworkManager", "Stopped node: " + node_id);
        } catch (const std::exception& e) {
            LOG_ERROR("NetworkManager", "Error stopping node " + node_id + ": " + e.what());
        }
    }
}

void NetworkManager::run_consensus_monitor() {
    LOG_INFO("NetworkManager", "Consensus monitor started");
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        try {
            sync_chains();
        } catch (const std::exception& e) {
            LOG_ERROR("NetworkManager", "Error in consensus monitor: " + std::string(e.what()));
        }
    }
    
    LOG_INFO("NetworkManager", "Consensus monitor stopped");
}

void NetworkManager::sync_chains() {
    auto all_nodes = get_all_nodes();
    if (all_nodes.empty()) return;
    
    // Get the chain with the most blocks (longest chain)
    BlockchainNode* best_node = all_nodes[0];
    int max_height = best_node->get_blockchain().get_chain().size();
    
    for (auto node : all_nodes) {
        int height = node->get_blockchain().get_chain().size();
        if (height > max_height) {
            max_height = height;
            best_node = node;
        }
    }
    
    // Sync other nodes to the best chain
    const auto& best_chain = best_node->get_blockchain().get_chain();
    
    for (auto node : all_nodes) {
        if (node == best_node) continue;
        
        const auto& node_chain = node->get_blockchain().get_chain();
        if (node_chain.size() < best_chain.size()) {
            // Node is behind - request sync
            LOG_DEBUG("NetworkManager", "Syncing " + node->get_node_id() + " with " + best_node->get_node_id());
            node->request_chain_sync(best_node->get_node_id());
            
            // Apply the sync
            std::vector<Block> blocks_to_add(
                best_chain.begin() + node_chain.size(),
                best_chain.end()
            );
            
            if (!blocks_to_add.empty()) {
                node->handle_chain_sync(blocks_to_add);
            }
        }
    }
}

bool NetworkManager::is_network_synced(int max_height_diff) const {
    auto all_nodes = get_all_nodes();
    if (all_nodes.size() < 2) return true;  // Single node is always synced
    
    int min_height = INT_MAX;
    int max_height = 0;
    
    for (auto node : all_nodes) {
        int height = node->get_blockchain().get_chain().size();
        min_height = std::min(min_height, height);
        max_height = std::max(max_height, height);
    }
    
    int diff = max_height - min_height;
    return diff <= max_height_diff;
}

void NetworkManager::wait_for_sync(int timeout_seconds) {
    LOG_INFO("NetworkManager", "Waiting for network to sync (timeout: " + std::to_string(timeout_seconds) + "s)");
    
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        if (is_network_synced()) {
            LOG_INFO("NetworkManager", "Network is synced!");
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        
        if (elapsed > timeout_seconds) {
            LOG_WARN("NetworkManager", "Sync timeout after " + std::to_string(timeout_seconds) + "s");
            return;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int NetworkManager::get_network_height() const {
    auto all_nodes = get_all_nodes();
    int max_height = 0;
    
    for (auto node : all_nodes) {
        int height = node->get_blockchain().get_chain().size();
        max_height = std::max(max_height, height);
    }
    
    return max_height;
}

std::map<std::string, int> NetworkManager::get_chain_heights() const {
    std::map<std::string, int> heights;
    auto all_nodes = get_all_nodes();
    
    for (auto node : all_nodes) {
        int height = node->get_blockchain().get_chain().size();
        heights[node->get_node_id()] = height;
    }
    
    return heights;
}

std::map<std::string, bool> NetworkManager::get_sync_status() const {
    std::map<std::string, bool> status;
    auto all_nodes = get_all_nodes();
    int network_height = get_network_height();
    
    for (auto node : all_nodes) {
        int height = node->get_blockchain().get_chain().size();
        bool is_synced = (height == network_height);
        status[node->get_node_id()] = is_synced;
    }
    
    return status;
}

std::vector<Block> NetworkManager::resolve_fork(const std::vector<std::vector<Block>>& competing_chains) {
    // Longest chain rule: return the chain with most blocks
    if (competing_chains.empty()) return {};
    
    const auto& longest_chain = *std::max_element(
        competing_chains.begin(),
        competing_chains.end(),
        [](const std::vector<Block>& a, const std::vector<Block>& b) {
            return a.size() < b.size();
        }
    );
    
    LOG_INFO("NetworkManager", "Resolved fork: selected chain with " + std::to_string(longest_chain.size()) + " blocks");
    return longest_chain;
}
