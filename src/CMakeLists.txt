add_executable(lft_cli main.cpp)

target_include_directories(lft_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Placeholder for linking libraries (OpenSSL, etc.)
# find_package(OpenSSL REQUIRED)
# target_link_libraries(lft_cli PRIVATE OpenSSL::SSL OpenSSL::Crypto)
