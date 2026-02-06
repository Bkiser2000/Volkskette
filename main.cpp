#include "network_manager.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <set>

void print_header(const std::string& text) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  " << text << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

void print_network_status(NetworkManager* network) {
    auto heights = network->get_chain_heights();
    auto sync_status = network->get_sync_status();
    
    std::cout << "\n📊 Network Status:\n";
    std::cout << "   Network Height: " << network->get_network_height() << " blocks\n";
    
    for (const auto& [node_id, height] : heights) {
        bool synced = sync_status[node_id];
        std::string status = synced ? "✓ SYNCED" : "⚠ BEHIND";
        std::cout << "   " << node_id << ": " << height << " blocks [" << status << "]\n";
    }
    
    std::cout << std::endl;
}

int main() {
    // Initialize logging
    Logger::enable_console_logging();
    Logger::set_level(LogLevel::INFO);
    
    print_header("Volkskette P2P Blockchain Network Demo");
    std::cout << "Multi-Node Consensus with Distributed Synchronization\n" << std::endl;
    
    try {
        // Create network manager
        std::cout << "🔧 Initializing network..." << std::endl;
        auto network = std::make_unique<NetworkManager>();
        
        // Create three blockchain nodes
        std::cout << "📍 Creating nodes...\n";
        BlockchainNode* node1 = network->create_node("Alice", 8001, 4);
        BlockchainNode* node2 = network->create_node("Bob", 8002, 4);
        BlockchainNode* node3 = network->create_node("Charlie", 8003, 4);
        
        std::cout << "✓ Created 3 nodes\n" << std::endl;
        
        // Connect peers in a mesh topology
        print_header("Connecting Peers");
        network->connect_peers("Alice", "Bob");
        network->connect_peers("Bob", "Charlie");
        network->connect_peers("Charlie", "Alice");
        
        // Start the network
        print_header("Starting Network");
        std::cout << "🚀 Starting all nodes...\n";
        network->start_all_nodes();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        print_network_status(network.get());
        
        // ===== Test 1: Single node mining =====
        print_header("Test 1: Mining on Single Node");
        std::cout << "Alice mines a block...\n";
        
        // Create and broadcast a transaction
        std::string creator = "0xCreator";
        std::string caller = "0xCaller";
        
        node1->get_blockchain().create_account(creator, 1000.0);
        node1->get_blockchain().create_account(caller, 500.0);
        
        Transaction tx = node1->get_blockchain().create_transaction(
            creator, caller, 100.0, 1.0, "private_key_1"
        );
        
        node1->validate_and_add_transaction(tx);
        node1->broadcast_transaction(tx);
        
        // Mine a block
        std::cout << "Mining block...\n";
        node1->mine_pending_transactions();
        
        print_network_status(network.get());
        
        // ===== Test 2: Network synchronization =====
        print_header("Test 2: Network Synchronization");
        std::cout << "Waiting for nodes to synchronize...\n";
        
        network->wait_for_sync(15);  // 15 second timeout
        
        print_network_status(network.get());
        
        // ===== Test 3: Distributed transactions =====
        print_header("Test 3: Distributed Transactions");
        
        // Create accounts on all nodes by syncing
        std::cout << "Syncing initial state across network...\n";
        
        // Node 2 (Bob) creates an account and mines
        std::cout << "Bob creates an account and broadcasts a transaction...\n";
        node2->get_blockchain().create_account("0xBob", 750.0);
        
        Transaction tx2 = node2->get_blockchain().create_transaction(
            "0xBob", caller, 50.0, 0.5, "private_key_2"
        );
        
        node2->validate_and_add_transaction(tx2);
        node2->broadcast_transaction(tx2);
        
        std::cout << "Bob mines a block...\n";
        node2->mine_pending_transactions();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Node 3 (Charlie) also mines
        std::cout << "Charlie mines a block...\n";
        node3->get_blockchain().create_account("0xCharlie", 600.0);
        
        Transaction tx3 = node3->get_blockchain().create_transaction(
            "0xCharlie", caller, 75.0, 0.5, "private_key_3"
        );
        
        node3->validate_and_add_transaction(tx3);
        node3->broadcast_transaction(tx3);
        node3->mine_pending_transactions();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Wait for full synchronization
        std::cout << "Synchronizing distributed blocks...\n";
        network->wait_for_sync(15);
        
        print_network_status(network.get());
        
        // ===== Test 4: Consensus verification =====
        print_header("Test 4: Consensus Verification");
        
        std::cout << "Verifying consensus across all nodes...\n";
        
        auto alice_chain = node1->get_blockchain().get_chain();
        auto bob_chain = node2->get_blockchain().get_chain();
        auto charlie_chain = node3->get_blockchain().get_chain();
        
        bool consensus = (alice_chain.size() == bob_chain.size() && 
                         bob_chain.size() == charlie_chain.size());
        
        if (consensus) {
            std::cout << "✅ CONSENSUS ACHIEVED! All nodes agree on chain length: " 
                     << alice_chain.size() << " blocks\n";
        } else {
            std::cout << "⚠ WARNING: Nodes have different chain lengths:\n";
            std::cout << "   Alice: " << alice_chain.size() << " blocks\n";
            std::cout << "   Bob: " << bob_chain.size() << " blocks\n";
            std::cout << "   Charlie: " << charlie_chain.size() << " blocks\n";
        }
        
        // ===== Test 5: Chain validity =====
        print_header("Test 5: Chain Validation");
        
        std::cout << "Validating blockchain on each node:\n";
        
        bool alice_valid = node1->get_blockchain().is_chain_valid();
        bool bob_valid = node2->get_blockchain().is_chain_valid();
        bool charlie_valid = node3->get_blockchain().is_chain_valid();
        
        std::cout << "   Alice's chain: " << (alice_valid ? "✅ VALID" : "❌ INVALID") << "\n";
        std::cout << "   Bob's chain: " << (bob_valid ? "✅ VALID" : "❌ INVALID") << "\n";
        std::cout << "   Charlie's chain: " << (charlie_valid ? "✅ VALID" : "❌ INVALID") << "\n";
        
        // ===== Test 6: Account balances =====
        print_header("Test 6: Distributed Account State");
        
        std::cout << "Account balances across the network:\n";
        
        auto alice_balances = node1->get_blockchain().get_all_balances();
        auto bob_balances = node2->get_blockchain().get_all_balances();
        auto charlie_balances = node3->get_blockchain().get_all_balances();
        
        // Get all unique accounts
        std::set<std::string> all_accounts;
        for (const auto& [addr, _] : alice_balances) all_accounts.insert(addr);
        for (const auto& [addr, _] : bob_balances) all_accounts.insert(addr);
        for (const auto& [addr, _] : charlie_balances) all_accounts.insert(addr);
        
        std::cout << std::fixed << std::setprecision(2);
        for (const auto& account : all_accounts) {
            double alice_bal = alice_balances.count(account) ? alice_balances.at(account) : 0.0;
            double bob_bal = bob_balances.count(account) ? bob_balances.at(account) : 0.0;
            double charlie_bal = charlie_balances.count(account) ? charlie_balances.at(account) : 0.0;
            
            bool consistent = (alice_bal == bob_bal && bob_bal == charlie_bal);
            std::string status = consistent ? "✓" : "✗";
            
            std::cout << "   " << status << " " << account << ": "
                     << "Alice=" << alice_bal << ", "
                     << "Bob=" << bob_bal << ", "
                     << "Charlie=" << charlie_bal << "\n";
        }
        
        // Final status
        print_header("Demo Summary");
        
        std::cout << "✅ Multi-Node Consensus: " << (consensus ? "WORKING" : "NEEDS WORK") << "\n";
        std::cout << "✅ Network Synchronization: " << (network->is_network_synced() ? "SYNCED" : "OUT OF SYNC") << "\n";
        std::cout << "✅ Chain Validation: " << (alice_valid && bob_valid && charlie_valid ? "VALID" : "INVALID") << "\n";
        
        std::cout << "\n📊 Final Network Statistics:\n";
        std::cout << "   Total Blocks: " << alice_chain.size() << "\n";
        std::cout << "   Total Accounts: " << all_accounts.size() << "\n";
        std::cout << "   Peers Connected: " << node1->get_peers().size() << "\n";
        
        // Cleanup
        print_header("Shutting Down");
        std::cout << "Stopping network...\n";
        network->stop_all_nodes();
        
        std::cout << "\n✅ Demo completed successfully!\n" << std::endl;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Main", "Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

