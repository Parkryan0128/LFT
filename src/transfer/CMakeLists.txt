find_package(Msquic REQUIRED)

add_library(lft_transfer
  quic_server.cpp
  quic_client.cpp
)

target_include_directories(lft_transfer PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lft_transfer PUBLIC Msquic::msquic)

lft_set_msquic_rpath(lft_transfer)
