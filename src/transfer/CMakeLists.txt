find_package(Msquic REQUIRED)

add_library(lft_transfer
  quic_server.cpp
  quic_client.cpp
)

target_include_directories(lft_transfer PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lft_transfer PUBLIC Msquic::msquic)

target_compile_definitions(lft_transfer PRIVATE
  LFT_CERT_FILE="${CMAKE_SOURCE_DIR}/certs/lft-cert.pem"
  LFT_KEY_FILE="${CMAKE_SOURCE_DIR}/certs/lft-key.pem"
)

lft_set_msquic_rpath(lft_transfer)
