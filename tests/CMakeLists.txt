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
