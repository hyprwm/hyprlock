#include "Dbus.hpp"
#include "../helpers/Log.hpp"

CDbus::CDbus(bool enable) {
    if (!enable)
        return;
    try {
        m_connection        = sdbus::createSystemBusConnection();
        m_freedesktopLogin1 = sdbus::createProxy(*m_connection, sdbus::ServiceName{"org.freedesktop.login1"}, sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (sdbus::Error& e) {
        Log::logger->log(Log::ERR, "dbus: failed to create connection or login proxy ({})", e.what());
        m_freedesktopLogin1.reset();
        m_connection.reset();
    }
}
