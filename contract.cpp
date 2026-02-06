#include "contract.hpp"
#include <iostream>
#include <algorithm>
#include <ctime>

// ============= SmartContract =============

SmartContract::SmartContract(const std::string& address, const std::string& creator,
                           const std::string& name, const std::string& language)
    : address_(address), creator_(creator), name_(name), language_(language) {
    creation_timestamp_ = std::time(nullptr);
}

StackValue SmartContract::get_storage(const std::string& key) const {
    auto it = storage_.find(key);
    if (it == storage_.end()) {
        return StackValue(0);  // Default to 0
    }
    return it->second;
}

void SmartContract::set_storage(const std::string& key, const StackValue& value) {
    storage_[key] = value;
}

void SmartContract::add_instruction(const Instruction& instr) {
    bytecode_.push_back(instr);
}

void SmartContract::load_bytecode(const std::vector<uint8_t>& code) {
    // Parse bytecode into instructions
    size_t offset = 0;
    while (offset < code.size()) {
        Instruction instr = Instruction::deserialize(code, offset);
        bytecode_.push_back(instr);
    }
}

std::vector<uint8_t> SmartContract::serialize_bytecode() const {
    std::vector<uint8_t> result;
    for (const auto& instr : bytecode_) {
        auto serialized = instr.serialize();
        result.insert(result.end(), serialized.begin(), serialized.end());
    }
    return result;
}

json SmartContract::to_json() const {
    json j;
    j["address"] = address_;
    j["creator"] = creator_;
    j["name"] = name_;
    j["language"] = language_;
    j["creation_timestamp"] = creation_timestamp_;
    j["storage"] = json::object();
    for (const auto& [key, value] : storage_) {
        j["storage"][key] = value.to_json();
    }
    j["bytecode_size"] = bytecode_.size();
    j["source_code"] = source_code_;
    return j;
}

SmartContract SmartContract::from_json(const json& j) {
    SmartContract contract(
        j["address"].get<std::string>(),
        j["creator"].get<std::string>(),
        j["name"].get<std::string>(),
        j["language"].get<std::string>()
    );
    return contract;
}

// ============= ContractVM =============

ContractVM::ContractVM() : pc_(0), halted_(false), contract_(nullptr) {
    context_.gas_remaining = 1000000;  // 1M gas default
}

void ContractVM::push_stack(const StackValue& value) {
    if (stack_.size() >= 1024) {
        throw std::runtime_error("Stack overflow");
    }
    stack_.push_back(value);
}

StackValue ContractVM::pop_stack() {
    if (stack_.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    StackValue value = stack_.back();
    stack_.pop_back();
    return value;
}

StackValue ContractVM::peek_stack() const {
    if (stack_.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    return stack_.back();
}

void ContractVM::calculate_gas_cost(OpCode opcode) {
    // Gas costs for different operations
    switch (opcode) {
        case OpCode::STOP:
        case OpCode::PUSH:
        case OpCode::POP:
            context_.gas_cost = 3;
            break;
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::MOD:
            context_.gas_cost = 5;
            break;
        case OpCode::LOAD:
        case OpCode::STORE:
            context_.gas_cost = 20;
            break;
        case OpCode::SLOAD:
        case OpCode::SSTORE:
            context_.gas_cost = 100;
            break;
        case OpCode::TRANSFER:
            context_.gas_cost = 50;
            break;
        default:
            context_.gas_cost = 10;
    }
    
    context_.gas_remaining -= context_.gas_cost;
    if (context_.gas_remaining < 0) {
        throw std::runtime_error("Out of gas");
    }
}

void ContractVM::handle_push(const Instruction& instr) {
    if (instr.args.empty()) {
        // If no args, push 0 as default
        push_stack(StackValue(0));
    } else {
        // Parse value from args (handle variable length)
        int64_t value = 0;
        size_t size = std::min(sizeof(int64_t), instr.args.size());
        std::memcpy(&value, instr.args.data(), size);
        push_stack(StackValue(value));
    }
}

void ContractVM::handle_pop() {
    pop_stack();
}

void ContractVM::handle_dup() {
    push_stack(peek_stack());
}

void ContractVM::handle_swap() {
    if (stack_.size() < 2) throw std::runtime_error("Insufficient stack for SWAP");
    std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
}

void ContractVM::handle_add() {
    auto b = pop_stack().as_integer();
    auto a = pop_stack().as_integer();
    push_stack(StackValue(a + b));
}

void ContractVM::handle_sub() {
    auto b = pop_stack().as_integer();
    auto a = pop_stack().as_integer();
    push_stack(StackValue(a - b));
}

void ContractVM::handle_mul() {
    auto b = pop_stack().as_integer();
    auto a = pop_stack().as_integer();
    push_stack(StackValue(a * b));
}

void ContractVM::handle_div() {
    auto b = pop_stack().as_integer();
    auto a = pop_stack().as_integer();
    if (b == 0) throw std::runtime_error("Division by zero");
    push_stack(StackValue(a / b));
}

void ContractVM::handle_mod() {
    auto b = pop_stack().as_integer();
    auto a = pop_stack().as_integer();
    if (b == 0) throw std::runtime_error("Division by zero");
    push_stack(StackValue(a % b));
}

void ContractVM::handle_load() {
    if (stack_.empty()) {
        // If stack is empty, just load default value
        push_stack(StackValue(0));
    } else {
        auto key = pop_stack().as_string();
        auto value = contract_->get_storage(key);
        push_stack(value);
    }
}

void ContractVM::handle_store() {
    if (stack_.size() < 2) {
        // Insufficient items on stack, just clear
        return;
    }
    auto value = pop_stack();
    auto key = pop_stack().as_string();
    contract_->set_storage(key, value);
}

void ContractVM::handle_sload() {
    auto key = pop_stack().as_string();
    auto value = context_.storage[key];
    push_stack(value);
}

void ContractVM::handle_sstore() {
    auto value = pop_stack();
    auto key = pop_stack().as_string();
    context_.storage[key] = value;
}

void ContractVM::handle_transfer() {
    auto amount = pop_stack().as_integer();
    auto to = pop_stack().as_string();
    
    if (context_.balances[context_.caller] < amount) {
        throw std::runtime_error("Insufficient balance for transfer");
    }
    
    context_.balances[context_.caller] -= amount;
    context_.balances[to] += amount;
    push_stack(StackValue(1));  // Success
}

void ContractVM::handle_balance() {
    auto address = pop_stack().as_string();
    auto balance = context_.balances[address];
    push_stack(StackValue(static_cast<int64_t>(balance)));
}

void ContractVM::handle_call(const Instruction& instr) {
    // Simplified call handling
    auto function_id = pop_stack().as_integer();
    push_stack(StackValue(0));  // Placeholder return value
}

void ContractVM::handle_return() {
    halted_ = true;
}

bool ContractVM::execute(SmartContract* contract, const ExecutionContext& context) {
    contract_ = contract;
    context_ = context;
    pc_ = 0;
    halted_ = false;
    stack_.clear();
    
    try {
        while (!halted_ && pc_ < contract_->get_bytecode().size()) {
            if (!step()) {
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        error_message_ = e.what();
        return false;
    }
}

bool ContractVM::step() {
    if (pc_ >= contract_->get_bytecode().size()) {
        halted_ = true;
        return true;
    }
    
    try {
        const auto& instr = contract_->get_bytecode()[pc_];
        calculate_gas_cost(instr.opcode);
        
        switch (instr.opcode) {
            case OpCode::STOP:
                halted_ = true;
                break;
            case OpCode::PUSH:
                handle_push(instr);
                break;
            case OpCode::POP:
                handle_pop();
                break;
            case OpCode::DUP:
                handle_dup();
                break;
            case OpCode::SWAP:
                handle_swap();
                break;
            case OpCode::ADD:
                handle_add();
                break;
            case OpCode::SUB:
                handle_sub();
                break;
            case OpCode::MUL:
                handle_mul();
                break;
            case OpCode::DIV:
                handle_div();
                break;
            case OpCode::MOD:
                handle_mod();
                break;
            case OpCode::LOAD:
                handle_load();
                break;
            case OpCode::STORE:
                handle_store();
                break;
            case OpCode::SLOAD:
                handle_sload();
                break;
            case OpCode::SSTORE:
                handle_sstore();
                break;
            case OpCode::TRANSFER:
                handle_transfer();
                break;
            case OpCode::BALANCE:
                handle_balance();
                break;
            case OpCode::CALL:
                handle_call(instr);
                break;
            case OpCode::RETURN:
                handle_return();
                break;
            case OpCode::REVERT:
                throw std::runtime_error("Contract execution reverted");
            case OpCode::ASSERT:
                if (!peek_stack().as_boolean()) {
                    throw std::runtime_error("Assertion failed");
                }
                pop_stack();
                break;
            default:
                throw std::runtime_error("Unknown opcode");
        }
        
        pc_++;
        return true;
    } catch (const std::exception& e) {
        error_message_ = e.what();
        return false;
    }
}

StackValue ContractVM::get_result() const {
    if (stack_.empty()) {
        return StackValue(0);
    }
    return stack_.back();
}

int64_t ContractVM::get_gas_used() const {
    return 1000000 - context_.gas_remaining;
}

void ContractVM::register_native_function(const std::string& name, 
                                         std::function<void()> func) {
    native_functions_[name] = func;
}

// ============= ContractManager =============

std::string ContractManager::deploy_contract(const std::string& creator,
                                            const std::string& name,
                                            const std::string& language,
                                            const std::vector<uint8_t>& bytecode) {
    // Generate contract address
    uint64_t nonce = contract_nonces_[creator]++;
    std::string address = generate_contract_address(creator, nonce);
    
    // Create contract
    auto contract = std::make_shared<SmartContract>(address, creator, name, language);
    contract->load_bytecode(bytecode);
    
    // Store contract
    contracts_[address] = contract;
    address_to_creator_[address] = creator;
    
    return address;
}

SmartContract* ContractManager::get_contract(const std::string& address) {
    auto it = contracts_.find(address);
    if (it == contracts_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const SmartContract* ContractManager::get_contract(const std::string& address) const {
    auto it = contracts_.find(address);
    if (it == contracts_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool ContractManager::contract_exists(const std::string& address) const {
    return contracts_.find(address) != contracts_.end();
}

std::vector<std::string> ContractManager::get_contracts_by_creator(const std::string& creator) const {
    std::vector<std::string> result;
    for (const auto& [address, creator_addr] : address_to_creator_) {
        if (creator_addr == creator) {
            result.push_back(address);
        }
    }
    return result;
}

std::vector<std::string> ContractManager::get_all_contracts() const {
    std::vector<std::string> result;
    for (const auto& [address, _] : contracts_) {
        result.insert(result.begin(), address);
    }
    return result;
}

void ContractManager::delete_contract(const std::string& address) {
    contracts_.erase(address);
    address_to_creator_.erase(address);
}

std::string ContractManager::generate_contract_address(const std::string& creator, uint64_t nonce) {
    // Simplified: hash(creator + nonce)
    return "0x" + creator.substr(0, 10) + "_" + std::to_string(nonce);
}

json ContractManager::get_all_contracts_json() const {
    json j = json::object();
    for (const auto& [address, contract] : contracts_) {
        j[address] = contract->to_json();
    }
    return j;
}

// ============= ContractCompiler =============

std::vector<uint8_t> ContractCompiler::compile_bytecode(const std::vector<Instruction>& instructions) {
    std::vector<uint8_t> bytecode;
    for (const auto& instr : instructions) {
        auto serialized = instr.serialize();
        bytecode.insert(bytecode.end(), serialized.begin(), serialized.end());
    }
    return bytecode;
}

Instruction ContractCompiler::create_push_instruction(int64_t value) {
    Instruction instr;
    instr.opcode = OpCode::PUSH;
    instr.args.resize(8);
    std::memcpy(instr.args.data(), &value, 8);
    return instr;
}

Instruction ContractCompiler::create_store_instruction(const std::string& key) {
    Instruction instr;
    instr.opcode = OpCode::STORE;
    instr.args.assign(key.begin(), key.end());
    return instr;
}

Instruction ContractCompiler::create_load_instruction(const std::string& key) {
    Instruction instr;
    instr.opcode = OpCode::LOAD;
    instr.args.assign(key.begin(), key.end());
    return instr;
}

// Example: Counter contract
// Increments a counter stored in state
std::vector<Instruction> ContractCompiler::create_counter_contract() {
    std::vector<Instruction> instructions;
    
    // PUSH 0x01          (push 1)
    Instruction push1;
    push1.opcode = OpCode::PUSH;
    push1.args.resize(8);
    int64_t one = 1;
    std::memcpy(push1.args.data(), &one, 8);
    instructions.push_back(push1);
    
    // LOAD "counter"     (load current counter)
    Instruction load;
    load.opcode = OpCode::LOAD;
    std::string key = "counter";
    load.args.assign(key.begin(), key.end());
    instructions.push_back(load);
    
    // ADD                (add 1 to counter)
    Instruction add_instr;
    add_instr.opcode = OpCode::ADD;
    instructions.push_back(add_instr);
    
    // STORE "counter"    (store back to state)
    Instruction store;
    store.opcode = OpCode::STORE;
    store.args.assign(key.begin(), key.end());
    instructions.push_back(store);
    
    // RETURN
    Instruction ret;
    ret.opcode = OpCode::RETURN;
    instructions.push_back(ret);
    
    return instructions;
}

// Example: Token contract
// Manages balance and transfers
std::vector<Instruction> ContractCompiler::create_token_contract() {
    std::vector<Instruction> instructions;
    
    // Initialize with 1000 tokens
    Instruction push_supply;
    push_supply.opcode = OpCode::PUSH;
    push_supply.args.resize(8);
    int64_t supply = 1000;
    std::memcpy(push_supply.args.data(), &supply, 8);
    instructions.push_back(push_supply);
    
    Instruction store_supply;
    store_supply.opcode = OpCode::STORE;
    std::string key = "total_supply";
    store_supply.args.assign(key.begin(), key.end());
    instructions.push_back(store_supply);
    
    Instruction ret;
    ret.opcode = OpCode::RETURN;
    instructions.push_back(ret);
    
    return instructions;
}

// Example: Escrow contract
// Holds funds in escrow
std::vector<Instruction> ContractCompiler::create_escrow_contract() {
    std::vector<Instruction> instructions;
    
    // Push 0 as placeholder
    Instruction push_zero;
    push_zero.opcode = OpCode::PUSH;
    push_zero.args.resize(8);
    int64_t zero = 0;
    std::memcpy(push_zero.args.data(), &zero, 8);
    instructions.push_back(push_zero);
    
    // Store as escrow amount
    Instruction store_escrow;
    store_escrow.opcode = OpCode::STORE;
    std::string key = "escrow_amount";
    store_escrow.args.assign(key.begin(), key.end());
    instructions.push_back(store_escrow);
    
    Instruction ret;
    ret.opcode = OpCode::RETURN;
    instructions.push_back(ret);
    
    return instructions;
}
