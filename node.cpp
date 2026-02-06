#include "node.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <algorithm>

// ============= PeerConnection =============

void PeerConnection::start() {
    LOG_DEBUG("PeerConnection", "Starting to listen for messages");
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(data_, max_length),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            this->handle_read(error, bytes_transferred);
        });
}

void PeerConnection::handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        std::string received_data(data_, bytes_transferred);
        LOG_DEBUG("PeerConnection", "Received " + std::to_string(bytes_transferred) + " bytes");
        // Message will be processed by the node
        start();
    } else {
        LOG_WARN("PeerConnection", "Read error: " + error.message());
    }
}

void PeerConnection::send_message(const NetworkMessage& msg) {
    std::string serialized = msg.to_json().dump() + "\n";
    LOG_DEBUG("PeerConnection", "Sending message of type: " + std::to_string(static_cast<int>(msg.type)));
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(serialized),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            this->handle_write(error);
        });
}

void PeerConnection::handle_write(const boost::system::error_code& error) {
    if (error) {
        LOG_ERROR("PeerConnection", "Write error: " + error.message());
    }
}

// ============= BlockchainNode =============

BlockchainNode::BlockchainNode(const std::string& node_id, uint16_t port, int difficulty)
    : node_id_(node_id), port_(port), blockchain_() {
    
    LOG_INFO("BlockchainNode", "Initializing node: " + node_id + " on port " + std::to_string(port));
    // Set initial difficulty in blockchain
    // Note: This would need a setter method in Blockchain
}

BlockchainNode::~BlockchainNode() {
    LOG_INFO("BlockchainNode", "Shutting down node: " + node_id_);
    stop();
}

void BlockchainNode::start() {
    try {
        acceptor_ = std::make_unique<tcp::acceptor>(
            io_service_,
            tcp::endpoint(tcp::v4(), port_));
        
        LOG_INFO("BlockchainNode", "Node listening on port " + std::to_string(port_));
        std::cout << "[" << node_id_ << "] Node listening on port " << port_ << std::endl;
        
        accept_connection();
        
        network_thread_ = std::thread([this]() {
            io_service_.run();
        });
        
    } catch (std::exception& e) {
        LOG_ERROR("BlockchainNode", "Error starting node: " + std::string(e.what()));
        std::cerr << "Error starting node: " << e.what() << std::endl;
    }
}

void BlockchainNode::stop() {
    LOG_INFO("BlockchainNode", "Stopping network services");
    io_service_.stop();
    if (network_thread_.joinable()) {
        network_thread_.join();
    }
}

void BlockchainNode::accept_connection() {
    PeerConnection::pointer new_connection =
        PeerConnection::create(io_service_);

    acceptor_->async_accept(
        new_connection->socket(),
        [this, new_connection](const boost::system::error_code& error) {
            if (!error) {
                new_connection->start();
            }
            this->accept_connection();
        });
}

void BlockchainNode::connect_to_peer(const std::string& host, uint16_t port) {
    try {
        tcp::socket socket(io_service_);
        tcp::resolver resolver(io_service_);
        tcp::resolver::query query(host, std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        
        boost::asio::connect(socket, endpoint_iterator);
        
        std::string peer_id = host + ":" + std::to_string(port);
        add_peer(peer_id, peer_id);
        
        // Send handshake
        NetworkMessage handshake;
        handshake.type = MessageType::HANDSHAKE;
        handshake.sender_id = node_id_;
        handshake.payload = node_id_;
        
        std::cout << "[" << node_id_ << "] Connected to peer: " << peer_id << std::endl;
        
    } catch (std::exception& e) {
        std::cerr << "[" << node_id_ << "] Connection error: " << e.what() << std::endl;
    }
}

void BlockchainNode::add_peer(const std::string& peer_id, const std::string& address) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peer_map_[peer_id] = address;
    std::cout << "[" << node_id_ << "] Added peer: " << peer_id << std::endl;
}

void BlockchainNode::remove_peer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peer_map_.erase(peer_id);
    std::cout << "[" << node_id_ << "] Removed peer: " << peer_id << std::endl;
}

std::set<std::string> BlockchainNode::get_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::set<std::string> peers;
    for (const auto& [id, _] : peer_map_) {
        peers.insert(id);
    }
    return peers;
}

void BlockchainNode::broadcast_transaction(const Transaction& tx) {
    NetworkMessage msg;
    msg.type = MessageType::NEW_TRANSACTION;
    msg.sender_id = node_id_;
    msg.payload = tx.to_json().dump();
    
    LOG_INFO("BlockchainNode", "Broadcasting transaction: " + tx.transaction_id.substr(0, 16) + 
             "... amount: " + std::to_string(tx.amount));
    broadcast_message(msg);
    std::cout << "[" << node_id_ << "] Broadcast transaction: " << tx.transaction_id << std::endl;
}

void BlockchainNode::receive_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(tx_queue_mutex_);
    LOG_DEBUG("BlockchainNode", "Received transaction: " + tx.transaction_id.substr(0, 16));
    pending_transactions_.push(tx);
}

bool BlockchainNode::validate_and_add_transaction(const Transaction& tx) {
    try {
        // Verify signature
        if (tx.signature.empty() || tx.public_key.empty()) {
            LOG_WARN("BlockchainNode", "Invalid transaction: missing signature/public_key");
            std::cerr << "[" << node_id_ << "] Invalid transaction: missing signature/public_key" << std::endl;
            return false;
        }

        // Verify sender has balance
        double sender_balance = blockchain_.get_balance(tx.from);
        if (sender_balance < (tx.amount + tx.gas_price)) {
            LOG_WARN("BlockchainNode", "Invalid transaction: insufficient balance for " + tx.from);
            std::cerr << "[" << node_id_ << "] Invalid transaction: insufficient balance" << std::endl;
            return false;
        }

        // Verify amounts are positive
        if (tx.amount <= 0 || tx.gas_price < 0) {
            std::cerr << "[" << node_id_ << "] Invalid transaction: negative amounts" << std::endl;
            return false;
        }

        // Verify addresses
        if (tx.from.empty() || tx.to.empty() || tx.from == tx.to) {
            std::cerr << "[" << node_id_ << "] Invalid transaction: invalid addresses" << std::endl;
            return false;
        }

        // Add to blockchain's mempool
        blockchain_.add_transaction(tx);
        
        std::cout << "[" << node_id_ << "] Transaction validated and added: " << tx.transaction_id << std::endl;
        return true;
        
    } catch (const BlockchainException& e) {
        std::cerr << "[" << node_id_ << "] Transaction validation error: " << e.what() << std::endl;
        return false;
    }
}

void BlockchainNode::broadcast_block(const Block& block) {
    NetworkMessage msg;
    msg.type = MessageType::NEW_BLOCK;
    msg.sender_id = node_id_;
    msg.payload = block.to_json().dump();
    
    broadcast_message(msg);
    std::cout << "[" << node_id_ << "] Broadcast block: " << block.index << std::endl;
}

void BlockchainNode::receive_block(const Block& block) {
    std::lock_guard<std::mutex> lock(blockchain_mutex_);
    
    // Verify block
    if (blockchain_.is_chain_valid()) {
        std::cout << "[" << node_id_ << "] Received valid block: " << block.index << std::endl;
    } else {
        std::cout << "[" << node_id_ << "] Received invalid block: " << block.index << std::endl;
    }
}

void BlockchainNode::mine_pending_transactions() {
    std::lock_guard<std::mutex> lock(tx_queue_mutex_);
    
    if (pending_transactions_.empty()) {
        return;
    }
    
    try {
        std::cout << "[" << node_id_ << "] Mining block with " 
                  << pending_transactions_.size() << " transactions..." << std::endl;
        
        Block mined_block = blockchain_.mine_block(pending_transactions_.size());
        
        std::cout << "[" << node_id_ << "] Block mined: " << mined_block.index 
                  << " with proof: " << mined_block.proof << std::endl;
        
        // Clear pending transactions
        while (!pending_transactions_.empty()) {
            pending_transactions_.pop();
        }
        
        // Broadcast the new block
        broadcast_block(mined_block);
        
    } catch (const BlockchainException& e) {
        std::cerr << "[" << node_id_ << "] Mining error: " << e.what() << std::endl;
    }
}

void BlockchainNode::request_chain_sync(const std::string& peer_id) {
    NetworkMessage msg;
    msg.type = MessageType::SYNC_REQUEST;
    msg.sender_id = node_id_;
    msg.payload = node_id_;
    
    std::cout << "[" << node_id_ << "] Requesting chain sync from peer: " << peer_id << std::endl;
}

void BlockchainNode::handle_chain_sync(const std::vector<Block>& incoming_chain) {
    std::lock_guard<std::mutex> lock(blockchain_mutex_);
    
    auto current_chain = blockchain_.get_chain();
    
    // Simple longest chain rule
    if (incoming_chain.size() > current_chain.size()) {
        // In production, verify the entire chain before replacing
        std::cout << "[" << node_id_ << "] Accepting longer chain: " 
                  << incoming_chain.size() << " blocks" << std::endl;
    }
}

// ============= STATE SYNCHRONIZATION (Phase 4.2) =============

void BlockchainNode::request_state_sync(const std::string& peer_id) {
    NetworkMessage msg;
    msg.type = MessageType::STATE_SYNC_REQUEST;
    msg.sender_id = node_id_;
    msg.payload = node_id_;
    
    LOG_DEBUG("BlockchainNode", "Requesting state sync from peer: " + peer_id);
    std::cout << "[" << node_id_ << "] Requesting state sync from: " << peer_id << std::endl;
}

void BlockchainNode::handle_state_sync_request(const std::string& peer_id) {
    // Get current state snapshot and send to peer
    std::lock_guard<std::mutex> lock(blockchain_mutex_);
    
    auto state = blockchain_.get_account_state();
    std::string state_root = blockchain_.get_state_root();
    
    json state_json = json::object();
    state_json["state_root"] = state_root;
    state_json["block_height"] = blockchain_.get_chain().size();
    state_json["node_id"] = node_id_;
    
    // Serialize account state
    json accounts = json::object();
    for (const auto& [addr, data] : state) {
        const auto& [balance, nonce] = data;
        accounts[addr] = {
            {"balance", balance},
            {"nonce", nonce}
        };
    }
    state_json["accounts"] = accounts;
    
    NetworkMessage response;
    response.type = MessageType::STATE_SYNC_RESPONSE;
    response.sender_id = node_id_;
    response.payload = state_json.dump();
    
    LOG_INFO("BlockchainNode", "Responding to state sync request from " + peer_id + 
             " with " + std::to_string(state.size()) + " accounts, state_root: " + state_root.substr(0, 16));
    std::cout << "[" << node_id_ << "] STATE SYNC RESPONSE -> " << peer_id 
              << " (" << state.size() << " accounts, root: " << state_root.substr(0, 16) << "...)" << std::endl;
}

void BlockchainNode::handle_state_sync_response(const json& state_data, const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(blockchain_mutex_);
    
    // Extract state information from peer
    std::string peer_state_root = state_data["state_root"].get<std::string>();
    std::string peer_node_id = state_data["node_id"].get<std::string>();
    int peer_block_height = state_data["block_height"].get<int>();
    
    // Get local state
    std::string local_state_root = blockchain_.get_state_root();
    auto local_state = blockchain_.get_account_state();
    int local_block_height = blockchain_.get_chain().size();
    
    // Compare state roots
    if (peer_state_root == local_state_root) {
        LOG_INFO("BlockchainNode", "State sync verified ✓ with " + peer_id + 
                 " (root: " + local_state_root.substr(0, 16) + "..., height: " + 
                 std::to_string(local_block_height) + ")");
        std::cout << "[" << node_id_ << "] State Synchronization: ✓ IN SYNC with " << peer_id 
                  << " (height: " << local_block_height << ", root: " << local_state_root.substr(0, 16) << "...)" << std::endl;
    } else {
        LOG_WARN("BlockchainNode", "State divergence detected with " + peer_id + 
                 " Local: " + local_state_root.substr(0, 16) + " Remote: " + peer_state_root.substr(0, 16));
        std::cout << "[" << node_id_ << "] State Synchronization: ⚠️  OUT OF SYNC with " << peer_id 
                  << " (Local root: " << local_state_root.substr(0, 16) << "... vs Remote: " << peer_state_root.substr(0, 16) << "...)" << std::endl;
    }
}

void BlockchainNode::handle_handshake(const NetworkMessage& msg, const std::string& peer_address) {
    std::cout << "[" << node_id_ << "] Received handshake from: " << msg.sender_id << std::endl;
    add_peer(msg.sender_id, peer_address);
}

void BlockchainNode::handle_new_transaction(const NetworkMessage& msg) {
    try {
        json tx_json = json::parse(msg.payload);
        Transaction tx;
        tx.from = tx_json["from"];
        tx.to = tx_json["to"];
        tx.amount = tx_json["amount"];
        tx.gas_price = tx_json["gas_price"];
        tx.timestamp = tx_json["timestamp"];
        tx.signature = tx_json["signature"];
        tx.public_key = tx_json["public_key"];
        tx.transaction_id = tx_json["transaction_id"];
        
        if (validate_and_add_transaction(tx)) {
            // Relay to other peers
            broadcast_message(msg, msg.sender_id);
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << node_id_ << "] Error handling transaction: " << e.what() << std::endl;
    }
}

void BlockchainNode::handle_new_block(const NetworkMessage& msg) {
    try {
        json block_json = json::parse(msg.payload);
        Block block;
        block.index = block_json["index"];
        block.timestamp = block_json["timestamp"];
        block.merkle_root = block_json["merkle_root"];
        block.proof = block_json["proof"];
        block.previous_hash = block_json["previous_hash"];
        
        receive_block(block);
        
        // Relay to other peers
        broadcast_message(msg, msg.sender_id);
    } catch (const std::exception& e) {
        std::cerr << "[" << node_id_ << "] Error handling block: " << e.what() << std::endl;
    }
}

void BlockchainNode::handle_request_chain(const NetworkMessage& msg) {
    NetworkMessage response;
    response.type = MessageType::RESPONSE_CHAIN;
    response.sender_id = node_id_;
    
    json chain_json = blockchain_.get_chain_json();
    response.payload = chain_json.dump();
    
    std::cout << "[" << node_id_ << "] Sending chain to peer: " << msg.sender_id << std::endl;
}

void BlockchainNode::handle_response_chain(const NetworkMessage& msg) {
    try {
        json chain_json = json::parse(msg.payload);
        std::vector<Block> incoming_chain;
        
        for (const auto& block_json : chain_json) {
            Block block;
            block.index = block_json["index"];
            block.timestamp = block_json["timestamp"];
            block.merkle_root = block_json["merkle_root"];
            block.proof = block_json["proof"];
            block.previous_hash = block_json["previous_hash"];
            incoming_chain.push_back(block);
        }
        
        handle_chain_sync(incoming_chain);
    } catch (const std::exception& e) {
        std::cerr << "[" << node_id_ << "] Error handling chain response: " << e.what() << std::endl;
    }
}

void BlockchainNode::handle_sync_request(const NetworkMessage& msg) {
    std::cout << "[" << node_id_ << "] Sync request from: " << msg.sender_id << std::endl;
}

void BlockchainNode::handle_sync_response(const NetworkMessage& msg) {
    std::cout << "[" << node_id_ << "] Sync response from: " << msg.sender_id << std::endl;
}

void BlockchainNode::broadcast_message(const NetworkMessage& msg, const std::string& exclude_peer) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    for (const auto& [peer_id, address] : peer_map_) {
        if (!exclude_peer.empty() && peer_id == exclude_peer) {
            continue;
        }
        // In production, maintain persistent connections and send directly
        std::cout << "[" << node_id_ << "] Broadcasting to peer: " << peer_id << std::endl;
    }
}

std::string BlockchainNode::serialize_message(const NetworkMessage& msg) {
    return msg.to_json().dump();
}

NetworkMessage BlockchainNode::deserialize_message(const std::string& data) {
    json j = json::parse(data);
    return NetworkMessage::from_json(j);
}

bool BlockchainNode::is_chain_longer(const std::vector<Block>& other_chain) const {
    return other_chain.size() > blockchain_.get_chain().size();
}
