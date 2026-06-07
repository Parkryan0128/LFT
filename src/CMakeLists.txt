# Legacy UDP socket layer (used for mDNS in a later milestone).
add_library(lft_net
  net/udp_socket.cpp
)
target_include_directories(lft_net PUBLIC ${CMAKE_SOURCE_DIR}/include)

# QUIC file transfer engine (Milestone 1+).
add_subdirectory(transfer)

# Build the CLI executable
add_executable(lft_cli main.cpp)

target_include_directories(lft_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lft_cli PRIVATE lft_net)

# Placeholder for linking libraries (OpenSSL, etc.)
# find_package(OpenSSL REQUIRED)
# target_link_libraries(lft_cli PRIVATE OpenSSL::SSL OpenSSL::Crypto)
