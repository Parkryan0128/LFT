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
target_link_libraries(lft_cli PRIVATE lft_net lft_transfer)

# Embed the msquic/OpenSSL rpath so the CLI runs without DYLD overrides.
lft_set_msquic_rpath(lft_cli)
