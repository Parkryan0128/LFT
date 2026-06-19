# Tests CMakeLists.txt
if(NOT BUILD_TESTS)
  return()
endif()

include(GoogleTest)

set(LFT_TEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Legacy UDP loopback (hand-rolled main, not GTest).
add_executable(checkpoint1_test legacy/checkpoint1_test.cpp)
target_include_directories(checkpoint1_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(checkpoint1_test PRIVATE lft_net)
add_test(NAME Legacy.Checkpoint1_UDP_Loopback COMMAND checkpoint1_test)

# CLI subprocess tests (argument parsing + early validation).
add_executable(cli_e2e_test e2e/cli_test.cpp)
target_include_directories(cli_e2e_test PRIVATE ${LFT_TEST_INCLUDE_DIR})
target_link_libraries(cli_e2e_test PRIVATE GTest::gtest_main)
add_dependencies(cli_e2e_test lft_cli)
target_compile_definitions(cli_e2e_test PRIVATE
  "LFT_CLI_PATH=\"${CMAKE_BINARY_DIR}/src/lft_cli\"")
gtest_discover_tests(cli_e2e_test)

if(BUILD_MSQUIC_TESTS)
  find_package(Msquic REQUIRED)

  # Smoke: libmsquic loads.
  add_executable(msquic_smoke_test smoke/msquic_smoke_test.cpp)
  target_link_libraries(msquic_smoke_test PRIVATE Msquic::msquic GTest::gtest_main)
  lft_set_msquic_rpath(msquic_smoke_test)
  gtest_discover_tests(msquic_smoke_test)

  # Unit: wire protocol + SHA-256 (no networking).
  add_executable(transfer_protocol_test unit/transfer_protocol_test.cpp)
  target_include_directories(transfer_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LFT_TEST_INCLUDE_DIR})
  target_link_libraries(transfer_protocol_test PRIVATE lft_transfer GTest::gtest_main)
  lft_set_msquic_rpath(transfer_protocol_test)
  gtest_discover_tests(transfer_protocol_test)

  # Integration: QUIC server/client, echo, file transfer, edge cases.
  add_executable(quic_transfer_test integration/quic_transfer_test.cpp)
  target_include_directories(quic_transfer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LFT_TEST_INCLUDE_DIR})
  target_link_libraries(quic_transfer_test PRIVATE lft_transfer GTest::gtest_main)
  lft_set_msquic_rpath(quic_transfer_test)
  gtest_discover_tests(quic_transfer_test)

  add_executable(quic_transfer_edgecases_test
    integration/quic_transfer_edgecases_test.cpp
    integration/quic_test_access.cpp)
  target_include_directories(quic_transfer_edgecases_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LFT_TEST_INCLUDE_DIR})
  target_link_libraries(quic_transfer_edgecases_test PRIVATE lft_transfer GTest::gtest_main)
  lft_set_msquic_rpath(quic_transfer_edgecases_test)
  gtest_discover_tests(quic_transfer_edgecases_test)
endif()
