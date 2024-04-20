cmake_minimum_required(VERSION 3.19)

set(VERSION 0.3.0)

project(hyprlock
    DESCRIPTION "A gpu-accelerated screen lock for Hyprland"
    VERSION ${VERSION}
)

set(CMAKE_MESSAGE_LOG_LEVEL "STATUS")

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
    message(STATUS "Configuring hyprlock in Debug with CMake")
    add_compile_definitions(HYPRLAND_DEBUG)
else()
    add_compile_options(-O3)
    message(STATUS "Configuring hyprlock in Release with CMake")
endif()

include_directories(
  .
  "protocols/"
)

include(GNUInstallDirs)

# configure
set(CMAKE_CXX_STANDARD 23)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-value
	-Wno-missing-field-initializers -Wno-narrowing)

# dependencies
message(STATUS "Checking deps...")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET wayland-client wayland-protocols wayland-egl hyprlang>=0.4.0 egl opengl xkbcommon libjpeg libwebp libmagic cairo pangocairo libdrm gbm)

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(hyprlock ${SRCFILES})
target_link_libraries(hyprlock PRIVATE pam rt Threads::Threads PkgConfig::deps OpenGL::EGL OpenGL::GL)

# protocols
find_program(WaylandScanner NAMES wayland-scanner)
message(STATUS "Found WaylandScanner at ${WaylandScanner}")
execute_process(
    COMMAND pkg-config --variable=pkgdatadir wayland-protocols
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "Found wayland-protocols at ${WAYLAND_PROTOCOLS_DIR}")

function(protocol protoPath protoName external)
    if (external)
        execute_process(
            COMMAND ${WaylandScanner} client-header ${protoPath} protocols/${protoName}-protocol.h
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        execute_process(
            COMMAND ${WaylandScanner} private-code ${protoPath} protocols/${protoName}-protocol.c
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        target_sources(hyprlock PRIVATE protocols/${protoName}-protocol.c)
    else()
        execute_process(
            COMMAND ${WaylandScanner} client-header ${WAYLAND_PROTOCOLS_DIR}/${protoPath} protocols/${protoName}-protocol.h
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        execute_process(
            COMMAND ${WaylandScanner} private-code ${WAYLAND_PROTOCOLS_DIR}/${protoPath} protocols/${protoName}-protocol.c
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        target_sources(hyprlock PRIVATE protocols/${protoName}-protocol.c)
    endif()
endfunction()

make_directory(${CMAKE_SOURCE_DIR}/protocols) # we don't ship any custom ones so the dir won't be there
protocol("staging/ext-session-lock/ext-session-lock-v1.xml" "ext-session-lock-v1" false)
protocol("staging/cursor-shape/cursor-shape-v1.xml" "cursor-shape-v1" false)
protocol("unstable/tablet/tablet-unstable-v2.xml" "tablet-unstable-v2" false)
protocol("staging/fractional-scale/fractional-scale-v1.xml" "fractional-scale-v1" false)
protocol("stable/viewporter/viewporter.xml" "viewporter" false)
protocol("protocols/wlr-screencopy-unstable-v1.xml" "wlr-screencopy-unstable-v1" true)
protocol("unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml" "linux-dmabuf-unstable-v1" false)

# Installation
install(TARGETS hyprlock)

install(FILES ${CMAKE_SOURCE_DIR}/pam/hyprlock DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)
