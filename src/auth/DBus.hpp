#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>

// Shared system D-Bus connection used by auth backends (fingerprint, visage).
// Lazily created by initDBus(); null if no backend requested it or setup failed.
// Dispatched from the main loop via processPendingEvent() — see hyprlock.cpp.
extern std::shared_ptr<sdbus::IConnection> g_dbus;

bool initDBus();
