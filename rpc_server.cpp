#include "rpc_server.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

// ============= RPC SESSION =============

void RPCSession::start() {
    LOG_DEBUG("RPCSession", "Starting RPC session");
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(data_, max_length),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            this->handle_read(error, bytes_transferred);
        });
}

void RPCSession::handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        read_buffer_.append(data_, bytes_transferred);

        // Look for complete HTTP request (ends with \r\n\r\n)
        size_t pos = read_buffer_.find("\r\n\r\n");
        if (pos == std::string::npos) {
            // Incomplete request, read more
            boost::asio::async_read(
                socket_,
                boost::asio::buffer(data_, max_length),
                [this](const boost::system::error_code& error, size_t bytes) {
                    this->handle_read(error, bytes);
                });
            return;
        }

        // Parse HTTP request
        std::istringstream request_stream(read_buffer_);
        std::string method, path, http_version;
        request_stream >> method >> path >> http_version;

        LOG_DEBUG("RPCSession", "Received " + method + " " + path);

        json response = json::object();

        try {
            if (method == "POST" && path == "/") {
                // Parse JSON-RPC 2.0 request
                size_t body_start = read_buffer_.find("\r\n\r\n") + 4;
                std::string body = read_buffer_.substr(body_start);
                
                json request = json::parse(body);
                std::string rpc_method = request["method"].get<std::string>();
                json params = request.contains("params") ? request["params"] : json::object();
                int id = request.contains("id") ? request["id"].get<int>() : -1;

                LOG_DEBUG("RPCSession", "RPC Method: " + rpc_method);

                // Route to appropriate handler
                if (rpc_method == "eth_getBalance") {
                    response = handle_getBalance(params);
                } else if (rpc_method == "eth_getAccountState") {
                    response = handle_getAccountState(params);
                } else if (rpc_method == "eth_getAccountNonce") {
                    response = handle_getAccountNonce(params);
                } else if (rpc_method == "eth_sendTransaction") {
                    response = handle_sendTransaction(params);
                } else if (rpc_method == "eth_getBlockByNumber") {
                    response = handle_getBlock(params);
                } else if (rpc_method == "eth_blockNumber") {
                    response = handle_getLatestBlockNumber(params);
                } else if (rpc_method == "eth_getBlockByHash") {
                    response = handle_getBlockByHash(params);
                } else if (rpc_method == "eth_getNetworkStats") {
                    response = handle_getNetworkStats(params);
                } else if (rpc_method == "net_peerCount") {
                    response = handle_getPeerCount(params);
                } else if (rpc_method == "eth_chainHeight") {
                    response = handle_getChainHeight(params);
                } else if (rpc_method == "eth_startMining") {
                    response = handle_startMining(params);
                } else if (rpc_method == "eth_stopMining") {
                    response = handle_stopMining(params);
                } else {
                    response = make_error("Method not found", -32601, id);
                }

                response["id"] = id;
            } else if (method == "GET" && path == "/health") {
                // Health check endpoint
                response["status"] = "ok";
                response["timestamp"] = std::to_string(std::time(nullptr));
                response["height"] = blockchain_->get_chain().size();
            } else {
                response["error"] = "Not found";
                response["status"] = 404;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("RPCSession", "Error handling request: " + std::string(e.what()));
            response = make_error("Invalid request: " + std::string(e.what()), -32600, -1);
        }

        send_response(response);

    } else {
        LOG_WARN("RPCSession", "Read error: " + error.message());
    }
}

void RPCSession::send_response(const json& response) {
    std::string body = response.dump();
    
    std::ostringstream http_response;
    http_response << "HTTP/1.1 200 OK\r\n"
                  << "Content-Type: application/json\r\n"
                  << "Content-Length: " << body.length() << "\r\n"
                  << "Connection: close\r\n"
                  << "\r\n"
                  << body;

    std::string response_str = http_response.str();
    LOG_DEBUG("RPCSession", "Sending response: " + body.substr(0, 100));

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(response_str),
        [this](const boost::system::error_code& error, size_t bytes) {
            this->handle_write(error);
        });
}

void RPCSession::handle_write(const boost::system::error_code& error) {
    if (error) {
        LOG_ERROR("RPCSession", "Write error: " + error.message());
    }
}

// ============= JSON-RPC METHOD HANDLERS =============

json RPCSession::handle_getBalance(const json& params) {
    try {
        std::string address = params[0].get<std::string>();
        double balance = blockchain_->get_balance(address);
        
        LOG_INFO("RPCSession", "getBalance(" + address + ") = " + std::to_string(balance));
        
        json result;
        result["address"] = address;
        result["balance"] = balance;
        return result;
    } catch (const std::exception& e) {
        return make_error("Invalid address", -32602, -1);
    }
}

json RPCSession::handle_getAccountState(const json& params) {
    try {
        std::string address = params[0].get<std::string>();
        double balance = blockchain_->get_balance(address);
        uint64_t nonce = blockchain_->get_account_nonce(address);
        
        LOG_INFO("RPCSession", "getAccountState(" + address + ")");
        
        json result;
        result["address"] = address;
        result["balance"] = balance;
        result["nonce"] = nonce;
        result["state_root"] = blockchain_->get_state_root().substr(0, 32);
        return result;
    } catch (const std::exception& e) {
        return make_error("Invalid address", -32602, -1);
    }
}

json RPCSession::handle_getAccountNonce(const json& params) {
    try {
        std::string address = params[0].get<std::string>();
        uint64_t nonce = blockchain_->get_account_nonce(address);
        
        LOG_INFO("RPCSession", "getAccountNonce(" + address + ") = " + std::to_string(nonce));
        
        json result;
        result["address"] = address;
        result["nonce"] = nonce;
        return result;
    } catch (const std::exception& e) {
        return make_error("Invalid address", -32602, -1);
    }
}

json RPCSession::handle_sendTransaction(const json& params) {
    try {
        std::string from = params["from"].get<std::string>();
        std::string to = params["to"].get<std::string>();
        double amount = params["amount"].get<double>();

        Transaction tx;
        tx.from = from;
        tx.to = to;
        tx.amount = amount;
        tx.nonce = blockchain_->get_account_nonce(from) + 1;
        
        // In production, would validate signature here
        blockchain_->add_transaction(tx);
        
        LOG_INFO("RPCSession", "sendTransaction: " + from + " -> " + to + " amount: " + std::to_string(amount));
        
        json result;
        result["tx_hash"] = tx.calculate_hash();
        result["status"] = "pending";
        result["nonce"] = tx.nonce;
        return result;
    } catch (const std::exception& e) {
        return make_error("Invalid transaction parameters", -32602, -1);
    }
}

json RPCSession::handle_getBlock(const json& params) {
    try {
        int block_number = params[0].get<int>();
        const auto& chain = blockchain_->get_chain();
        
        if (block_number < 0 || block_number >= (int)chain.size()) {
            return make_error("Block not found", -32602, -1);
        }
        
        const Block& block = chain[block_number];
        
        LOG_INFO("RPCSession", "getBlock(" + std::to_string(block_number) + ")");
        
        return block.to_json();
    } catch (const std::exception& e) {
        return make_error("Invalid block number", -32602, -1);
    }
}

json RPCSession::handle_getLatestBlockNumber(const json& params) {
    int height = blockchain_->get_chain().size();
    
    LOG_INFO("RPCSession", "blockNumber() = " + std::to_string(height));
    
    json result;
    result["number"] = height;
    result["height"] = height;
    return result;
}

json RPCSession::handle_getBlockByHash(const json& params) {
    try {
        std::string hash = params[0].get<std::string>();
        const auto& chain = blockchain_->get_chain();
        
        // Simple linear search (in production, would use index)
        for (size_t i = 0; i < chain.size(); ++i) {
            if (blockchain_->hash_block(chain[i]).substr(0, hash.length()) == hash) {
                LOG_INFO("RPCSession", "getBlockByHash(" + hash + ") = block #" + std::to_string(i));
                return chain[i].to_json();
            }
        }
        
        return make_error("Block not found", -32602, -1);
    } catch (const std::exception& e) {
        return make_error("Invalid block hash", -32602, -1);
    }
}

json RPCSession::handle_getNetworkStats(const json& params) {
    const auto& chain = blockchain_->get_chain();
    auto state = blockchain_->get_account_state();
    int peer_count = network_mgr_ ? network_mgr_->get_all_nodes().size() : 1;
    
    LOG_INFO("RPCSession", "getNetworkStats()");
    
    json result;
    result["total_blocks"] = chain.size();
    result["total_transactions"] = chain.size() > 0 ? 
        std::accumulate(chain.begin(), chain.end(), 0,
                       [](int sum, const Block& b) { return sum + b.transactions.size(); }) : 0;
    result["total_accounts"] = state.size();
    result["peer_count"] = peer_count;
    result["difficulty"] = blockchain_->get_difficulty();
    result["state_root"] = blockchain_->get_state_root().substr(0, 32);
    return result;
}

json RPCSession::handle_getPeerCount(const json& params) {
    int peer_count = network_mgr_ ? network_mgr_->get_all_nodes().size() : 1;
    
    LOG_INFO("RPCSession", "peerCount() = " + std::to_string(peer_count));
    
    json result;
    result["peer_count"] = peer_count;
    return result;
}

json RPCSession::handle_getChainHeight(const json& params) {
    int height = blockchain_->get_chain().size();
    
    LOG_INFO("RPCSession", "chainHeight() = " + std::to_string(height));
    
    json result;
    result["height"] = height;
    return result;
}

json RPCSession::handle_startMining(const json& params) {
    try {
        std::string miner_address = params[0].get<std::string>();
        
        LOG_INFO("RPCSession", "startMining(" + miner_address + ")");
        
        json result;
        result["status"] = "mining_started";
        result["miner_address"] = miner_address;
        return result;
    } catch (const std::exception& e) {
        return make_error("Invalid miner address", -32602, -1);
    }
}

json RPCSession::handle_stopMining(const json& params) {
    LOG_INFO("RPCSession", "stopMining()");
    
    json result;
    result["status"] = "mining_stopped";
    return result;
}

json RPCSession::make_response(const json& result, int id) {
    json response;
    response["jsonrpc"] = "2.0";
    response["result"] = result;
    response["id"] = id;
    return response;
}

json RPCSession::make_error(const std::string& message, int code, int id) {
    json response;
    response["jsonrpc"] = "2.0";
    response["error"]["code"] = code;
    response["error"]["message"] = message;
    if (id != -1) response["id"] = id;
    return response;
}

// ============= RPC SERVER =============

RPCServer::RPCServer(uint16_t port, Blockchain* blockchain, NetworkManager* network_mgr)
    : port_(port), running_(false), blockchain_(blockchain), network_mgr_(network_mgr) {
    LOG_INFO("RPCServer", "Initializing JSON-RPC server on port " + std::to_string(port));
}

RPCServer::~RPCServer() {
    stop();
}

void RPCServer::start() {
    if (running_) return;
    
    running_ = true;
    server_thread_ = std::thread(&RPCServer::run_server, this);
    LOG_INFO("RPCServer", "JSON-RPC server started on port " + std::to_string(port_));
}

void RPCServer::stop() {
    if (!running_) return;
    
    running_ = false;
    io_service_.stop();
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    LOG_INFO("RPCServer", "JSON-RPC server stopped");
}

void RPCServer::run_server() {
    try {
        tcp::endpoint endpoint(tcp::v4(), port_);
        acceptor_ = std::make_unique<tcp::acceptor>(io_service_, endpoint);
        
        LOG_INFO("RPCServer", "Listening for JSON-RPC requests on port " + std::to_string(port_));
        
        start_accept();
        io_service_.run();
    } catch (const std::exception& e) {
        LOG_ERROR("RPCServer", "Server error: " + std::string(e.what()));
    }
}

void RPCServer::start_accept() {
    RPCSession::pointer new_session = RPCSession::create(io_service_, blockchain_, network_mgr_);
    
    acceptor_->async_accept(
        new_session->socket(),
        [this, new_session](const boost::system::error_code& error) {
            this->handle_accept(new_session, error);
        });
}

void RPCServer::handle_accept(RPCSession::pointer new_session, const boost::system::error_code& error) {
    if (!error) {
        LOG_DEBUG("RPCServer", "New connection accepted");
        new_session->start();
    } else {
        LOG_WARN("RPCServer", "Accept error: " + error.message());
    }
    
    start_accept();
}
