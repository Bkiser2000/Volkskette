#ifndef RPC_SERVER_HPP
#define RPC_SERVER_HPP

#include "blockchain.hpp"
#include "network_manager.hpp"
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <memory>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using boost::asio::ip::tcp;

/**
 * RPCSession - Handles individual HTTP connections
 * Parses JSON-RPC 2.0 requests and returns responses
 */
class RPCSession : public boost::enable_shared_from_this<RPCSession> {
public:
    typedef boost::shared_ptr<RPCSession> pointer;

    static pointer create(boost::asio::io_service& io_service, 
                         Blockchain* blockchain,
                         NetworkManager* network_mgr) {
        return pointer(new RPCSession(io_service, blockchain, network_mgr));
    }

    tcp::socket::lowest_layer_type& socket() {
        return socket_.lowest_layer();
    }

    void start();

private:
    RPCSession(boost::asio::io_service& io_service,
              Blockchain* blockchain,
              NetworkManager* network_mgr)
        : socket_(io_service), blockchain_(blockchain), network_mgr_(network_mgr) {}

    void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
    void send_response(const json& response);
    void handle_write(const boost::system::error_code& error);

    // JSON-RPC request handlers
    json handle_getBalance(const json& params);
    json handle_getAccountState(const json& params);
    json handle_getAccountNonce(const json& params);
    json handle_sendTransaction(const json& params);
    json handle_getBlock(const json& params);
    json handle_getLatestBlockNumber(const json& params);
    json handle_getBlockByHash(const json& params);
    json handle_getNetworkStats(const json& params);
    json handle_getPeerCount(const json& params);
    json handle_getChainHeight(const json& params);
    json handle_startMining(const json& params);
    json handle_stopMining(const json& params);

    // Response builders
    json make_response(const json& result, int id);
    json make_error(const std::string& message, int code, int id);

    tcp::socket socket_;
    enum { max_length = 65536 };
    char data_[max_length];
    std::string read_buffer_;

    Blockchain* blockchain_;
    NetworkManager* network_mgr_;
};

/**
 * RPCServer - HTTP server for JSON-RPC 2.0 interface
 * Listens for incoming requests and manages RPC sessions
 */
class RPCServer {
public:
    RPCServer(uint16_t port, Blockchain* blockchain, NetworkManager* network_mgr);
    ~RPCServer();

    void start();
    void stop();
    bool is_running() const { return running_; }
    uint16_t get_port() const { return port_; }

private:
    void run_server();
    void start_accept();
    void handle_accept(RPCSession::pointer new_session, const boost::system::error_code& error);

    uint16_t port_;
    boost::asio::io_service io_service_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::thread server_thread_;
    bool running_;
    
    Blockchain* blockchain_;
    NetworkManager* network_mgr_;
};

#endif // RPC_SERVER_HPP
