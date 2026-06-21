find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(LFT_GUI_SOURCES
  main.cpp
  main_window.cpp
  home_page.cpp
  recv_page.cpp
  recv_worker.cpp
  send_page.cpp
  send_worker.cpp
  format.cpp
)

add_executable(lft_gui ${LFT_GUI_SOURCES})

target_include_directories(lft_gui PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(lft_gui PRIVATE
  Qt6::Widgets
  lft_common
  lft_net
  lft_transfer
)

set_target_properties(lft_gui PROPERTIES
  AUTOMOC ON
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.lft.app"
  MACOSX_BUNDLE_BUNDLE_NAME "LFT"
)

lft_set_msquic_rpath(lft_gui)
