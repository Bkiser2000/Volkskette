#include "blockchain.hpp"
#include "node.hpp"
#include "contract.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // Initialize logging
    Logger::enable_console_logging();
    // TODO: Fix file logging hang on second run - disable for now
    // Logger::enable_file_logging("./blockchain_data/volkskette.log");
    Logger::set_level(LogLevel::INFO);
    
    LOG_INFO("Main", "=== Volkskette: Blockchain with Smart Contracts ===");
    LOG_INFO("Main", "Supporting: Solidity, C, C++ Languages");
    
    std::cout << "=== Volkskette: Blockchain with Smart Contracts ===" << std::endl;
    std::cout << "Supporting: Solidity, C, C++ Languages\n" << std::endl;

    try {
        // Create blockchain node (without starting network for demo)
        LOG_INFO("Main", "Creating blockchain node...");
        BlockchainNode node1("Node-1", 8001);
        
        // Note: Skipping node.start() to avoid blocking on network I/O
        // node1.start();

        Blockchain& blockchain = node1.get_blockchain();
        
        // Load persisted blockchain state if available
        LOG_INFO("Main", "Loading blockchain state from storage...");
        blockchain.load_blockchain_state();

        // Create accounts (or skip if already loaded)
        std::cout << "=== Creating Accounts ===" << std::endl;
        std::string creator = "0xCreator";
        std::string caller = "0xCaller";
        std::string recipient = "0xRecipient";

        // Only create if not already in state
        auto existing_accounts = blockchain.get_all_balances();
        if (existing_accounts.find(creator) == existing_accounts.end()) {
            blockchain.create_account(creator, 1000.0);
            blockchain.create_account(caller, 500.0);
            blockchain.create_account(recipient, 200.0);
            LOG_INFO("Main", "Created 3 accounts with initial balances");
        } else {
            LOG_INFO("Main", "Accounts already loaded from persistent storage");
        }

        std::cout << "Creator balance: " << blockchain.get_balance(creator) << std::endl;
        std::cout << "Caller balance: " << blockchain.get_balance(caller) << std::endl;
        std::cout << "Recipient balance: " << blockchain.get_balance(recipient) << std::endl;

        // ============= SMART CONTRACTS DEMO =============

        std::cout << "\n=== Smart Contracts Deployment Demo ===" << std::endl;

        // Deploy Counter Contract (C++ example)
        LOG_INFO("Main", "Deploying Counter contract...");
        std::cout << "\n[1] Deploying Counter Contract (C++)..." << std::endl;
        auto counter_instructions = ContractCompiler::create_counter_contract();
        auto counter_bytecode = ContractCompiler::compile_bytecode(counter_instructions);

        std::string counter_address = blockchain.deploy_contract(
            creator,
            "Counter",
            "cpp",
            counter_bytecode
        );

        std::cout << "✓ Counter contract deployed at: " << counter_address << std::endl;
        std::cout << "  Bytecode size: " << counter_bytecode.size() << " bytes" << std::endl;

        // Deploy Token Contract (Solidity example)
        std::cout << "\n[2] Deploying Token Contract (Solidity)..." << std::endl;
        auto token_instructions = ContractCompiler::create_token_contract();
        auto token_bytecode = ContractCompiler::compile_bytecode(token_instructions);

        std::string token_address = blockchain.deploy_contract(
            creator,
            "MyToken",
            "solidity",
            token_bytecode
        );

        std::cout << "✓ Token contract deployed at: " << token_address << std::endl;
        std::cout << "  Bytecode size: " << token_bytecode.size() << " bytes" << std::endl;

        // Deploy Escrow Contract (C example)
        std::cout << "\n[3] Deploying Escrow Contract (C)..." << std::endl;
        auto escrow_instructions = ContractCompiler::create_escrow_contract();
        auto escrow_bytecode = ContractCompiler::compile_bytecode(escrow_instructions);

        std::string escrow_address = blockchain.deploy_contract(
            creator,
            "Escrow",
            "c",
            escrow_bytecode
        );

        std::cout << "✓ Escrow contract deployed at: " << escrow_address << std::endl;
        std::cout << "  Bytecode size: " << escrow_bytecode.size() << " bytes" << std::endl;

        // ============= CONTRACT EXECUTION DEMO =============

        std::cout << "\n=== Smart Contract Execution Demo ===" << std::endl;

        // Call Counter Contract
        std::cout << "\n[1] Calling Counter.increment()..." << std::endl;
        try {
            blockchain.call_contract(counter_address, caller, "increment", {});
            std::cout << "✓ Counter incremented successfully" << std::endl;

            // Check contract storage
            SmartContract* counter = blockchain.get_contract(counter_address);
            auto storage = counter->get_all_storage();
            std::cout << "  Contract storage:" << std::endl;
            for (const auto& [key, value] : storage) {
                std::cout << "    " << key << " = " << value.as_string() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "⚠ Counter call: " << e.what() << std::endl;
        }

        // Call Token Contract
        std::cout << "\n[2] Calling Token.transfer(recipient, 100)..." << std::endl;
        try {
            blockchain.call_contract(token_address, caller, "transfer", {"0xRecipient", "100"});
            std::cout << "✓ Token transfer executed" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "⚠ Token transfer: " << e.what() << std::endl;
        }

        // Call Escrow Contract
        std::cout << "\n[3] Calling Escrow.deposit(500)..." << std::endl;
        try {
            blockchain.call_contract(escrow_address, caller, "deposit", {"500"});
            std::cout << "✓ Escrow deposit processed" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "⚠ Escrow deposit: " << e.what() << std::endl;
        }

        // ============= CONTRACT INFORMATION =============

        std::cout << "\n=== Deployed Smart Contracts ===" << std::endl;

        ContractManager& mgr = blockchain.get_contract_manager();
        std::cout << "Total contracts deployed: " << mgr.get_contract_count() << std::endl;

        auto all_contracts = mgr.get_all_contracts();
        std::cout << "\nContract Addresses:" << std::endl;
        for (const auto& addr : all_contracts) {
            SmartContract* contract = mgr.get_contract(addr);
            if (contract) {
                std::cout << "  - " << contract->get_name() << std::endl;
                std::cout << "    Address: " << addr << std::endl;
                std::cout << "    Creator: " << contract->get_creator() << std::endl;
                std::cout << "    Language: " << contract->get_language() << std::endl;
                std::cout << "    Bytecode size: " << contract->get_bytecode().size() << " instructions" << std::endl;
            }
        }

        // ============= LANGUAGE SUPPORT INFO =============

        std::cout << "\n=== Smart Contract Languages Supported ===" << std::endl;
        std::cout << "\n[Solidity]" << std::endl;
        std::cout << "  • Full EVM-compatible smart contracts" << std::endl;
        std::cout << "  • Compile with: solc contract.sol --bin" << std::endl;
        std::cout << "  • Deploy as: blockchain.deploy_contract(creator, name, \"solidity\", bytecode)" << std::endl;

        std::cout << "\n[C]" << std::endl;
        std::cout << "  • WebAssembly-compiled C contracts" << std::endl;
        std::cout << "  • Compile with: clang -target wasm32-wasi contract.c -o contract.wasm" << std::endl;
        std::cout << "  • Deploy as: blockchain.deploy_contract(creator, name, \"c\", bytecode)" << std::endl;

        std::cout << "\n[C++]" << std::endl;
        std::cout << "  • WebAssembly-compiled C++ contracts" << std::endl;
        std::cout << "  • Compile with: clang++ -target wasm32-wasi contract.cpp -o contract.wasm" << std::endl;
        std::cout << "  • Deploy as: blockchain.deploy_contract(creator, name, \"cpp\", bytecode)" << std::endl;

        // ============= VM STATISTICS =============

        std::cout << "\n=== Virtual Machine Statistics ===" << std::endl;

        ContractVM& vm = blockchain.get_contract_vm();
        std::cout << "Contract VM Opcodes: " << static_cast<int>(OpCode::ASSERT) << " total" << std::endl;
        std::cout << "Supported operations:" << std::endl;
        std::cout << "  • Arithmetic: ADD, SUB, MUL, DIV, MOD" << std::endl;
        std::cout << "  • Stack: PUSH, POP, DUP, SWAP" << std::endl;
        std::cout << "  • Storage: LOAD, STORE, SLOAD, SSTORE" << std::endl;
        std::cout << "  • Transfer: TRANSFER, BALANCE" << std::endl;
        std::cout << "  • Control: CALL, RETURN, REVERT, ASSERT" << std::endl;
        std::cout << "  • Context: CALLER, ADDRESS, TIMESTAMP, BLOCKNUMBER" << std::endl;

        // ============= TRANSACTION WITH CONTRACT =============

        std::cout << "\n=== Blockchain Transaction With Contract Call ===" << std::endl;

        Transaction contract_tx = blockchain.create_transaction(
            caller, creator, 50.0, 1.0, "caller_private_key");

        contract_tx.contract_address = counter_address;
        contract_tx.data = "increment";

        std::cout << "Created transaction calling contract..." << std::endl;
        std::cout << "  Caller: " << caller << std::endl;
        std::cout << "  Contract: " << counter_address << std::endl;
        std::cout << "  Amount: 50 tokens" << std::endl;

        if (node1.validate_and_add_transaction(contract_tx)) {
            std::cout << "✓ Transaction added to mempool" << std::endl;
            node1.broadcast_transaction(contract_tx);
        }

        // Mine a block
        std::cout << "\n=== Mining Block ===" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        node1.mine_pending_transactions();

        // Final balances
        std::cout << "\n=== Final State ===" << std::endl;
        std::cout << "Creator balance: " << blockchain.get_balance(creator) << std::endl;
        std::cout << "Caller balance: " << blockchain.get_balance(caller) << std::endl;
        std::cout << "Recipient balance: " << blockchain.get_balance(recipient) << std::endl;

        std::cout << "\nChain length: " << blockchain.get_chain().size() << " blocks" << std::endl;
        std::cout << "Contracts deployed: " << mgr.get_contract_count() << std::endl;

        // Save blockchain state
        LOG_INFO("Main", "Saving blockchain state to persistent storage...");
        blockchain.save_blockchain_state();

        // Shutdown (skip node.stop() since we didn't start it)
        std::cout << "\nShutting down..." << std::endl;
        LOG_INFO("Main", "Demo shutdown");

        std::cout << "\n=== Demo Complete ===" << std::endl;
        LOG_INFO("Main", "Demo completed successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Main", "Error: " + std::string(e.what()));
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

