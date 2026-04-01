cmake_minimum_required(VERSION 3.27)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} VERSION)

project(
  hyprlock
  DESCRIPTION "A gpu-accelerated screen lock for Hyprland"
  VERSION ${VERSION})

set(CMAKE_MESSAGE_LOG_LEVEL "STATUS")
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring hyprlock in Debug with CMake")
  add_compile_definitions(HYPRLAND_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprlock in Release with CMake")
endif()

include_directories(. "protocols/")

include(GNUInstallDirs)

# configure
set(CMAKE_CXX_STANDARD 23)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-value
                    -Wno-missing-field-initializers -Wno-narrowing)




add_compile_definitions(HYPRLOCK_VERSION="${VERSION}")

if (DEFINED HYPRLOCK_COMMIT)
  add_compile_definitions(HYPRLOCK_COMMIT="${HYPRLOCK_COMMIT}")
else()
  # get git commit
  execute_process(
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND git rev-parse --short HEAD
  )
  add_compile_definitions(HYPRLOCK_COMMIT="${GIT_SHORT_HASH}")
endif()

if (DEFINED HYPRLOCK_VERSION_COMMIT)
  add_compile_definitions(HYPRLOCK_VERSION_COMMIT="${HYPRLOCK_VERSION_COMMIT}")
else()
  # get git commit of v$VERSION
  execute_process(
    OUTPUT_VARIABLE GIT_TAG_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND git rev-parse --short v${VERSION}
  )
  add_compile_definitions(HYPRLOCK_VERSION_COMMIT="${GIT_TAG_SHORT_HASH}")
endif()

message(STATUS "VERSION COMMIT: ${HYPRLOCK_VERSION_COMMIT}")
message(STATUS "COMMIT: ${HYPRLOCK_COMMIT}")

# position independent build: __FILE__
add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)

# dependencies
message(STATUS "Checking deps...")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL GLES3)
find_package(hyprwayland-scanner 0.4.4 REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  IMPORTED_TARGET
  wayland-client
  wayland-protocols>=1.35
  wayland-egl
  hyprlang>=0.6.0
  egl
  xkbcommon
  cairo
  pangocairo
  libdrm
  gbm
  hyprutils>=0.11.0
  sdbus-c++>=2.0.0
  libsodium
  hyprgraphics>=0.1.6)
find_library(PAM_FOUND NAMES pam libpam)
if(PAM_FOUND)
  set(PAM_LIB ${PAM_FOUND})
else()
  pkg_check_modules(PAM IMPORTED_TARGET pam libpam)
  if(PAM_FOUND)
    set(PAM_LIB PkgConfig::PAM)
  else()
    message(FATAL_ERROR "The required library libpam was not found.")
  endif()
endif()

message(STATUS "Found pam at ${PAM_LIB}")

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(hyprlock ${SRCFILES})
target_link_libraries(hyprlock PRIVATE ${PAM_LIB} rt Threads::Threads PkgConfig::deps
                                       OpenGL::EGL OpenGL::GLES3)

# protocols
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
message(STATUS "Found wayland-protocols at ${WAYLAND_PROTOCOLS_DIR}")
pkg_get_variable(WAYLAND_SCANNER_PKGDATA_DIR wayland-scanner pkgdatadir)
message(
  STATUS "Found wayland-scanner pkgdatadir at ${WAYLAND_SCANNER_PKGDATA_DIR}")

function(protocolnew protoPath protoName external)
  if(external)
    set(path ${CMAKE_SOURCE_DIR}/${protoPath})
  else()
    set(path ${WAYLAND_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/${protoName}.cpp
           ${CMAKE_SOURCE_DIR}/protocols/${protoName}.hpp
    COMMAND hyprwayland-scanner --client ${path}/${protoName}.xml
            ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(hyprlock PRIVATE protocols/${protoName}.cpp
                                   protocols/${protoName}.hpp)
endfunction()
function(protocolWayland)
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/wayland.cpp
           ${CMAKE_SOURCE_DIR}/protocols/wayland.hpp
    COMMAND hyprwayland-scanner --wayland-enums --client
            ${WAYLAND_SCANNER_PKGDATA_DIR}/wayland.xml ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(hyprlock PRIVATE protocols/wayland.cpp protocols/wayland.hpp)
endfunction()

make_directory(${CMAKE_SOURCE_DIR}/protocols) # we don't ship any custom ones so
                                              # the dir won't be there

protocolwayland()

protocolnew("protocols" "wlr-screencopy-unstable-v1" true)
protocolnew("staging/ext-session-lock" "ext-session-lock-v1" false)
protocolnew("stable/linux-dmabuf" "linux-dmabuf-v1" false)
protocolnew("staging/fractional-scale" "fractional-scale-v1" false)
protocolnew("stable/viewporter" "viewporter" false)
protocolnew("staging/cursor-shape" "cursor-shape-v1" false)
protocolnew("stable/tablet" "tablet-v2" false)

# hyprlock-pwhash
add_executable(hyprlock-pwhash "setpwhash/main.cpp")
target_link_libraries(hyprlock-pwhash PRIVATE sodium hyprutils)

# Installation
install(TARGETS hyprlock hyprlock-pwhash)

install(FILES ${CMAKE_SOURCE_DIR}/pam/hyprlock
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)

install(
  FILES ${CMAKE_SOURCE_DIR}/assets/example.conf
  DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/hypr
  RENAME hyprlock.conf)
