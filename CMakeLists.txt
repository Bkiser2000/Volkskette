cmake_minimum_required(VERSION 3.10)
project(Blockchain)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL REQUIRED)

# Add JSON Library include paths
include_directories(${CMAKE_SOURCE_DIR}/include)

# Create the blockchain library
add_library(blockchain blockchain.cpp node.cpp contract.cpp persistent_store.cpp utils/logger.cpp network_manager.cpp rpc_server.cpp)
target_link_libraries(blockchain PRIVATE OpenSSL::Crypto pthread)
target_include_directories(blockchain PUBLIC ${CMAKE_SOURCE_DIR})

# Create the executable
add_executable(blockchain_app main_p2p.cpp)
target_link_libraries(blockchain_app PRIVATE blockchain OpenSSL::Crypto pthread)

