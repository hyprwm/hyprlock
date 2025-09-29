cmake_minimum_required(VERSION 3.27)

project(lock_tester DESCRIPTION "Hyprlock test suite")

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)

find_package(PkgConfig REQUIRED)
find_package(hyprwayland-scanner 0.4.4 REQUIRED)

pkg_check_modules(lock_tester_deps REQUIRED IMPORTED_TARGET
  hyprland-protocols>=0.6.0
  hyprutils>=0.5.0
  wayland-client
  wayland-protocols>=1.35
)

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(lock_tester ${SRCFILES})

target_link_libraries(lock_tester PRIVATE PkgConfig::lock_tester_deps)

# protocols
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER_PKGDATA_DIR wayland-scanner pkgdatadir)
pkg_get_variable(HYPRLAND_PROTOCOLS hyprland-protocols pkgdatadir)
message(STATUS "Found hyprland-protocols at ${HYPRLAND_PROTOCOLS}")

make_directory(${CMAKE_SOURCE_DIR}/protocols)
target_include_directories(lock_tester PRIVATE ${CMAKE_SOURCE_DIR}/protocols)

# wayland client
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/wayland.cpp
           ${CMAKE_SOURCE_DIR}/protocols/wayland.hpp
    COMMAND hyprwayland-scanner --wayland-enums --client
            ${WAYLAND_SCANNER_PKGDATA_DIR}/wayland.xml ${CMAKE_SOURCE_DIR}/protocols/)
target_sources(lock_tester PRIVATE ${CMAKE_SOURCE_DIR}/protocols/wayland.cpp)

# hyprland-lock-notify-v1
add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/protocols/hyprland-lock-notify-v1.cpp
         ${CMAKE_SOURCE_DIR}/protocols/hyprland-lock-notify-v1.hpp
  COMMAND hyprwayland-scanner --client ${HYPRLAND_PROTOCOLS}/protocols/hyprland-lock-notify-v1.xml
          ${CMAKE_SOURCE_DIR}/protocols/)
target_sources(lock_tester PRIVATE ${CMAKE_SOURCE_DIR}/protocols/hyprland-lock-notify-v1.cpp)

install(TARGETS lock_tester)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/hyprland.conf
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/hypr)

file(GLOB_RECURSE TESTCONFIGS CONFIGURE_DEPENDS "configs/*.conf")
install(FILES ${TESTCONFIGS} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/hypr/configs)
