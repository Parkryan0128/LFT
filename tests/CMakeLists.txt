# Tests CMakeLists.txt
if(NOT BUILD_TESTS)
  return()
endif()

# Checkpoint 1: Local UDP Loopback Test
add_executable(checkpoint1_test checkpoint1_test.cpp)
target_include_directories(checkpoint1_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(checkpoint1_test PRIVATE lft_net)

# Register the test with CTest
add_test(NAME Checkpoint1_UDP_Loopback COMMAND checkpoint1_test)

# libmsquic smoke test (Homebrew: brew install libmsquic)
if(BUILD_MSQUIC_TESTS)
  find_package(Msquic REQUIRED)

  add_executable(msquic_smoke msquic_smoke.cpp)
  target_link_libraries(msquic_smoke PRIVATE Msquic::msquic)

  lft_set_msquic_rpath(msquic_smoke)

  add_test(NAME Msquic_Smoke COMMAND msquic_smoke)

  # Milestone 1 Step A — QUIC echo loopback (stubs until implemented).
  add_executable(quic_echo_test quic_echo_test.cpp)
  target_link_libraries(quic_echo_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(quic_echo_test)
  add_test(NAME Quic_Echo COMMAND quic_echo_test)

  add_executable(quic_file_transfer_test quic_file_transfer_test.cpp)
  target_link_libraries(quic_file_transfer_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(quic_file_transfer_test)
  add_test(NAME Quic_File_Transfer COMMAND quic_file_transfer_test)

  # Protocol + SHA-256 unit tests (no networking).
  add_executable(transfer_protocol_test transfer_protocol_test.cpp)
  target_link_libraries(transfer_protocol_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(transfer_protocol_test)
  add_test(NAME Transfer_Protocol COMMAND transfer_protocol_test)

  # File transfer edge cases (empty, multi-chunk, binary).
  add_executable(quic_file_edgecases_test quic_file_edgecases_test.cpp)
  target_link_libraries(quic_file_edgecases_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(quic_file_edgecases_test)
  add_test(NAME Quic_File_Edgecases COMMAND quic_file_edgecases_test)

  add_executable(quic_server_start_test quic_server_start_test.cpp)
  target_link_libraries(quic_server_start_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(quic_server_start_test)
  add_test(NAME Quic_Server_StartStop COMMAND quic_server_start_test)

  # Milestone 1 Step 4 — client connects to a running server.
  add_executable(quic_client_connect_test quic_client_connect_test.cpp)
  target_link_libraries(quic_client_connect_test PRIVATE lft_transfer)
  lft_set_msquic_rpath(quic_client_connect_test)
  add_test(NAME Quic_Client_Connect COMMAND quic_client_connect_test)
endif()
