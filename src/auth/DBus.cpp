#include "DBus.hpp"
#include "../helpers/Log.hpp"

std::shared_ptr<sdbus::IConnection> g_dbus;

bool initDBus() {
    if (g_dbus)
        return true;
    try {
        g_dbus = sdbus::createSystemBusConnection();
    } catch (const sdbus::Error& e) {
        Log::logger->log(Log::ERR, "dbus: failed to create system bus connection ({})", e.what());
        return false;
    }
    return true;
}
