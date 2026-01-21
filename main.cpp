#include "blockchain.hpp"
#include <iostream>

int main() {
    Blockchain blockchain;

    // Mine a few blocks
    std::cout << "Mining block 1..." << std::endl;
    Block block1 = blockchain.mine_block("Block 1 data");
    std::cout << "Block 1 mined with proof: " << block1.proof << std::endl;

    std::cout << "\nMining block 2..." << std::endl;
    Block block2 = blockchain.mine_block("Block 2 data");
    std::cout << "Block 2 mined with proof: " << block2.proof << std::endl;

    // Validate the blockchain
    std::cout << "\nValidating blockchain..." << std::endl;
    if (blockchain.is_chain_valid()) {
        std::cout << "Blockchain is valid!" << std::endl;
    } else {
        std::cout << "Blockchain is invalid!" << std::endl;
    }

    // Print the chain as JSON
    std::cout << "\nBlockchain:" << std::endl;
    std::cout << blockchain.get_chain_json().dump(2) << std::endl;

    return 0;
}

