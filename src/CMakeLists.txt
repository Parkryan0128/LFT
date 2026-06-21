# LAN networking: mDNS/DNS-SD discovery.
add_library(lft_net
  net/mdns.cpp
)
target_include_directories(lft_net PUBLIC ${CMAKE_SOURCE_DIR}/include)

# DNS-SD (Bonjour) is in libSystem on macOS; Linux needs avahi-compat-libdns_sd.
if(NOT APPLE)
  target_link_libraries(lft_net PUBLIC dns_sd)
endif()

# QUIC file transfer engine (Milestone 1+).
add_subdirectory(transfer)

# Build the CLI executable
add_executable(lft_cli main.cpp)

target_include_directories(lft_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lft_cli PRIVATE lft_net lft_transfer)

# Embed the msquic/OpenSSL rpath so the CLI runs without DYLD overrides.
lft_set_msquic_rpath(lft_cli)

if(BUILD_GUI)
  if(APPLE)
    execute_process(
      COMMAND brew --prefix qt
      OUTPUT_VARIABLE LFT_QT_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(LFT_QT_PREFIX)
      list(APPEND CMAKE_PREFIX_PATH "${LFT_QT_PREFIX}")
    endif()
  endif()
  add_subdirectory(gui)
endif()
