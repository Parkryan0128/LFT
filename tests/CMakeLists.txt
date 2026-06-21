# Tests CMakeLists.txt
if(NOT BUILD_TESTS)
  return()
endif()

include(GoogleTest)

set(LFT_TEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# CLI subprocess tests (argument parsing + early validation).
add_executable(cli_e2e_test e2e/cli_test.cpp)
target_include_directories(cli_e2e_test PRIVATE
  ${LFT_TEST_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(cli_e2e_test PRIVATE GTest::gtest_main lft_net)
add_dependencies(cli_e2e_test lft_cli)
target_compile_definitions(cli_e2e_test PRIVATE
  "LFT_CLI_PATH=\"${CMAKE_BINARY_DIR}/src/lft_cli\"")
gtest_discover_tests(cli_e2e_test)

# Integration: mDNS/DNS-SD advertise + browse (system Bonjour required).
add_executable(mdns_discovery_test integration/mdns_discovery_test.cpp)
target_include_directories(mdns_discovery_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mdns_discovery_test PRIVATE lft_net GTest::gtest_main)
gtest_discover_tests(mdns_discovery_test)

if(BUILD_INTEGRATION_TESTS)
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

  # Integration: QUIC connect, echo, file transfer, and edge cases.
  add_executable(quic_transfer_test
    integration/quic_transfer_test.cpp
    integration/quic_test_access.cpp)
  target_include_directories(quic_transfer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LFT_TEST_INCLUDE_DIR})
  target_link_libraries(quic_transfer_test PRIVATE lft_transfer GTest::gtest_main)
  lft_set_msquic_rpath(quic_transfer_test)
  gtest_discover_tests(quic_transfer_test)

  # Integration: mDNS discovery + QUIC file transfer.
  add_executable(mdns_transfer_test integration/mdns_transfer_test.cpp)
  target_include_directories(mdns_transfer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LFT_TEST_INCLUDE_DIR})
  target_link_libraries(mdns_transfer_test PRIVATE lft_net lft_transfer GTest::gtest_main)
  lft_set_msquic_rpath(mdns_transfer_test)
  gtest_discover_tests(mdns_transfer_test)
endif()
