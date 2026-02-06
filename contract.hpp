#ifndef CONTRACT_HPP
#define CONTRACT_HPP

#include <vector>
#include <string>
#include <map>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============= OPCODES =============
enum class OpCode : uint8_t {
    STOP = 0x00,          // End execution
    PUSH = 0x01,          // Push value to stack
    POP = 0x02,           // Pop from stack
    DUP = 0x03,           // Duplicate top stack item
    SWAP = 0x04,          // Swap top two stack items
    ADD = 0x05,           // Add top two stack values
    SUB = 0x06,           // Subtract
    MUL = 0x07,           // Multiply
    DIV = 0x08,           // Divide
    MOD = 0x09,           // Modulo
    LOAD = 0x0A,          // Load from storage
    STORE = 0x0B,         // Store to storage
    SLOAD = 0x0C,         // Load state
    SSTORE = 0x0D,        // Store state
    CALL = 0x0E,          // Call function
    RETURN = 0x0F,        // Return from function
    TRANSFER = 0x10,      // Transfer funds
    BALANCE = 0x11,       // Get account balance
    CALLER = 0x12,        // Get caller address
    ADDRESS = 0x13,       // Get contract address
    TIMESTAMP = 0x14,     // Get block timestamp
    BLOCKNUMBER = 0x15,   // Get block number
    REVERT = 0x16,        // Revert execution
    ASSERT = 0x17,        // Assert condition
};

// Stack value - can hold multiple types
struct StackValue {
    enum class Type : uint8_t {
        INTEGER,
        STRING,
        BOOLEAN,
        ADDRESS,
        BYTES
    };

    Type type;
    std::string data;

    StackValue() : type(Type::INTEGER), data("0") {}
    StackValue(int64_t value) : type(Type::INTEGER), data(std::to_string(value)) {}
    StackValue(const std::string& value, Type t = Type::STRING) : type(t), data(value) {}

    int64_t as_integer() const {
        if (type != Type::INTEGER) throw std::runtime_error("Type mismatch: not an integer");
        return std::stoll(data);
    }

    std::string as_string() const {
        return data;
    }

    bool as_boolean() const {
        if (type != Type::BOOLEAN) throw std::runtime_error("Type mismatch: not a boolean");
        return data == "true";
    }

    json to_json() const {
        json j;
        j["type"] = static_cast<int>(type);
        j["data"] = data;
        return j;
    }

    static StackValue from_json(const json& j) {
        StackValue sv;
        sv.type = static_cast<Type>(j["type"].get<int>());
        sv.data = j["data"].get<std::string>();
        return sv;
    }
};

// Contract bytecode instruction
struct Instruction {
    OpCode opcode;
    std::vector<uint8_t> args;  // Instruction arguments

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> result;
        result.push_back(static_cast<uint8_t>(opcode));
        result.insert(result.end(), args.begin(), args.end());
        return result;
    }

    static Instruction deserialize(const std::vector<uint8_t>& data, size_t& offset) {
        Instruction instr;
        instr.opcode = static_cast<OpCode>(data[offset++]);
        // Args would be parsed based on opcode type
        return instr;
    }
};

// Contract execution context
struct ExecutionContext {
    std::string caller;                        // Transaction sender
    std::string contract_address;              // This contract's address
    std::string origin;                        // Original transaction sender
    int64_t timestamp;                         // Block timestamp
    int64_t block_number;                      // Current block number
    std::map<std::string, StackValue> storage; // Contract storage
    std::map<std::string, double> balances;    // Account balances
    int64_t gas_remaining;                     // Gas left for execution
    int64_t gas_cost;                          // Gas cost of current instruction

    json to_json() const {
        json j;
        j["caller"] = caller;
        j["contract_address"] = contract_address;
        j["origin"] = origin;
        j["timestamp"] = timestamp;
        j["block_number"] = block_number;
        j["gas_remaining"] = gas_remaining;
        j["gas_cost"] = gas_cost;
        j["storage"] = json::object();
        for (const auto& [key, value] : storage) {
            j["storage"][key] = value.to_json();
        }
        return j;
    }
};

// Smart Contract
class SmartContract {
private:
    std::string address_;                      // Contract address
    std::string creator_;                      // Creator account
    std::vector<Instruction> bytecode_;        // Contract bytecode
    std::map<std::string, StackValue> storage_; // Persistent storage
    std::string name_;                         // Contract name
    std::string language_;                     // Source language (Solidity/C/C++)
    int64_t creation_timestamp_;
    std::string source_code_;                  // Original source code

public:
    SmartContract(const std::string& address, const std::string& creator,
                 const std::string& name, const std::string& language = "");
    
    // Getters
    std::string get_address() const { return address_; }
    std::string get_creator() const { return creator_; }
    std::string get_name() const { return name_; }
    std::string get_language() const { return language_; }
    std::vector<Instruction>& get_bytecode() { return bytecode_; }
    const std::vector<Instruction>& get_bytecode() const { return bytecode_; }
    
    // Storage operations
    StackValue get_storage(const std::string& key) const;
    void set_storage(const std::string& key, const StackValue& value);
    std::map<std::string, StackValue> get_all_storage() const { return storage_; }
    
    // Bytecode management
    void add_instruction(const Instruction& instr);
    void load_bytecode(const std::vector<uint8_t>& code);
    std::vector<uint8_t> serialize_bytecode() const;
    
    // Source code management
    void set_source_code(const std::string& code) { source_code_ = code; }
    std::string get_source_code() const { return source_code_; }
    
    json to_json() const;
    static SmartContract from_json(const json& j);
};

// Smart Contract Execution Engine (VM)
class ContractVM {
private:
    std::vector<StackValue> stack_;            // Execution stack
    size_t pc_;                                // Program counter
    ExecutionContext context_;
    SmartContract* contract_;
    std::map<std::string, std::function<void()>> native_functions_; // Native functions
    bool halted_;
    std::string error_message_;

    // VM instruction handlers
    void handle_push(const Instruction& instr);
    void handle_pop();
    void handle_dup();
    void handle_swap();
    void handle_add();
    void handle_sub();
    void handle_mul();
    void handle_div();
    void handle_mod();
    void handle_load();
    void handle_store();
    void handle_sload();
    void handle_sstore();
    void handle_transfer();
    void handle_balance();
    void handle_call(const Instruction& instr);
    void handle_return();

    // Helper functions
    void push_stack(const StackValue& value);
    StackValue pop_stack();
    StackValue peek_stack() const;
    void calculate_gas_cost(OpCode opcode);

public:
    ContractVM();
    
    // Execution
    bool execute(SmartContract* contract, const ExecutionContext& context);
    bool step();  // Execute single instruction
    
    // Status
    bool is_halted() const { return halted_; }
    std::string get_error() const { return error_message_; }
    const std::vector<StackValue>& get_stack() const { return stack_; }
    ExecutionContext get_context() const { return context_; }
    
    // Gas management
    int64_t get_gas_used() const;
    int64_t get_gas_remaining() const { return context_.gas_remaining; }
    
    // Result
    StackValue get_result() const;
    
    // Register native function
    void register_native_function(const std::string& name, 
                                 std::function<void()> func);
};

// Contract Manager - manages deployed contracts
class ContractManager {
private:
    std::map<std::string, std::shared_ptr<SmartContract>> contracts_;
    std::map<std::string, std::string> address_to_creator_;  // Quick lookup
    std::map<std::string, int64_t> contract_nonces_;  // Nonce for contract creation

public:
    // Contract deployment
    std::string deploy_contract(const std::string& creator, const std::string& name,
                               const std::string& language, const std::vector<uint8_t>& bytecode);
    
    // Contract access
    SmartContract* get_contract(const std::string& address);
    const SmartContract* get_contract(const std::string& address) const;
    bool contract_exists(const std::string& address) const;
    
    // Contract management
    std::vector<std::string> get_contracts_by_creator(const std::string& creator) const;
    std::vector<std::string> get_all_contracts() const;
    void delete_contract(const std::string& address);
    
    // Utilities
    std::string generate_contract_address(const std::string& creator, uint64_t nonce);
    
    json get_all_contracts_json() const;
    size_t get_contract_count() const { return contracts_.size(); }
};

// Contract compiler/helper - converts source to bytecode
class ContractCompiler {
public:
    // Compile simple bytecode from instructions
    static std::vector<uint8_t> compile_bytecode(const std::vector<Instruction>& instructions);
    
    // Create common contract patterns
    static std::vector<Instruction> create_counter_contract();
    static std::vector<Instruction> create_token_contract();
    static std::vector<Instruction> create_escrow_contract();
    
    // Helper to create instructions
    static Instruction create_push_instruction(int64_t value);
    static Instruction create_store_instruction(const std::string& key);
    static Instruction create_load_instruction(const std::string& key);
};

#endif // CONTRACT_HPP
