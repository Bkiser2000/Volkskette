#ifndef NODE_HPP
#define NODE_HPP

#include "blockchain.hpp"
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <memory>
#include <thread>
#include <set>
#include <map>

using boost::asio::ip::tcp;

// Message types for network communication
enum class MessageType : uint8_t {
    HANDSHAKE = 0,
    NEW_TRANSACTION = 1,
    NEW_BLOCK = 2,
    REQUEST_CHAIN = 3,
    RESPONSE_CHAIN = 4,
    SYNC_REQUEST = 5,
    SYNC_RESPONSE = 6,
    PEER_LIST = 7,
    ACK = 8,
    STATE_SYNC_REQUEST = 9,    // Phase 4.2: Request account state snapshot
    STATE_SYNC_RESPONSE = 10   // Phase 4.2: Response with account state
};

// Network message structure
struct NetworkMessage {
    MessageType type;
    std::string payload;
    std::string sender_id;
    
    json to_json() const {
        json j;
        j["type"] = static_cast<int>(type);
        j["payload"] = payload;
        j["sender_id"] = sender_id;
        return j;
    }
    
    static NetworkMessage from_json(const json& j) {
        NetworkMessage msg;
        msg.type = static_cast<MessageType>(j["type"].get<int>());
        msg.payload = j["payload"].get<std::string>();
        msg.sender_id = j["sender_id"].get<std::string>();
        return msg;
    }
};

// Connection handler for individual peers
class PeerConnection : public boost::enable_shared_from_this<PeerConnection> {
public:
    typedef boost::shared_ptr<PeerConnection> pointer;

    static pointer create(boost::asio::io_service& io_service) {
        return pointer(new PeerConnection(io_service));
    }

    tcp::socket::lowest_layer_type& socket() {
        return socket_.lowest_layer();
    }

    void start();
    void send_message(const NetworkMessage& msg);

private:
    PeerConnection(boost::asio::io_service& io_service) : socket_(io_service) {}

    void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
    void handle_write(const boost::system::error_code& error);

    tcp::socket socket_;
    enum { max_length = 65536 };
    char data_[max_length];
    std::string peer_id_;
};

// Main node class managing the blockchain network
class BlockchainNode {
public:
    BlockchainNode(const std::string& node_id, uint16_t port, int difficulty = 4);
    ~BlockchainNode();

    // Node operations
    void start();
    void stop();
    void connect_to_peer(const std::string& host, uint16_t port);
    
    // Transaction operations
    void broadcast_transaction(const Transaction& tx);
    void receive_transaction(const Transaction& tx);
    
    // Block operations
    void broadcast_block(const Block& block);
    void receive_block(const Block& block);
    
    // Synchronization
    void request_chain_sync(const std::string& peer_id);
    void handle_chain_sync(const std::vector<Block>& incoming_chain);
    
    // State Synchronization (Phase 4.2)
    void request_state_sync(const std::string& peer_id);
    void handle_state_sync_request(const std::string& peer_id);
    void handle_state_sync_response(const json& state_data, const std::string& peer_id);
    std::string get_state_root() const { return blockchain_.get_state_root(); }
    
    // Peer management
    void add_peer(const std::string& peer_id, const std::string& address);
    void remove_peer(const std::string& peer_id);
    std::set<std::string> get_peers() const;
    
    // Getters
    std::string get_node_id() const { return node_id_; }
    uint16_t get_port() const { return port_; }
    Blockchain& get_blockchain() { return blockchain_; }
    const Blockchain& get_blockchain() const { return blockchain_; }
    
    // Consensus & Mining
    void mine_pending_transactions();
    bool validate_and_add_transaction(const Transaction& tx);
    
private:
    std::string node_id_;
    uint16_t port_;
    Blockchain blockchain_;
    
    boost::asio::io_service io_service_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::thread network_thread_;
    
    // Peer management
    std::map<std::string, std::string> peer_map_;  // peer_id -> address
    mutable std::mutex peers_mutex_;
    mutable std::mutex blockchain_mutex_;
    
    // Transaction queue for pending transactions (memory-managed)
    std::queue<Transaction> pending_transactions_;
    mutable std::mutex tx_queue_mutex_;
    static constexpr size_t MAX_PENDING_TRANSACTIONS = 5000;  // Per-node capacity
    
    // Message tracking for resilience (with capacity limits)
    struct PendingMessage {
        NetworkMessage message;
        std::string target_peer;
        time_t sent_time;
        int retry_count;
        static const int MAX_RETRIES = 3;
        static const int RETRY_TIMEOUT_SECONDS = 5;
    };
    std::map<std::string, PendingMessage> pending_messages_;
    mutable std::mutex pending_msg_mutex_;
    static constexpr size_t MAX_PENDING_MESSAGES = 1000;
    
    // Message handlers
    void handle_handshake(const NetworkMessage& msg, const std::string& peer_address);
    void handle_new_transaction(const NetworkMessage& msg);
    void handle_new_block(const NetworkMessage& msg);
    void handle_request_chain(const NetworkMessage& msg);
    void handle_response_chain(const NetworkMessage& msg);
    void handle_sync_request(const NetworkMessage& msg);
    void handle_sync_response(const NetworkMessage& msg);
    
    // Utility functions
    void broadcast_message(const NetworkMessage& msg, const std::string& exclude_peer = "");
    std::string serialize_message(const NetworkMessage& msg);
    NetworkMessage deserialize_message(const std::string& data);
    void accept_connection();
    bool is_chain_longer(const std::vector<Block>& other_chain) const;
};

#endif // NODE_HPP
