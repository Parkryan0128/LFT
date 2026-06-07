cmake_minimum_required(VERSION 3.20)
project(lft VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_MSQUIC_TESTS "Build msquic smoke/integration tests" ON)

add_subdirectory(src)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
