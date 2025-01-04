#include "DBusManager.hpp"
#include "../helpers/Log.hpp"

DBusManager& DBusManager::getInstance() {
    static DBusManager instance;
    return instance;
}

DBusManager::DBusManager() {
    initializeConnection();
}

DBusManager::~DBusManager() {
    // Resources are automatically cleaned up.
}

void DBusManager::initializeConnection() {
    try {
        m_pConnection = sdbus::createSystemBusConnection();

        const sdbus::ServiceName destination{"org.freedesktop.login1"};
        const sdbus::ObjectPath loginPath{"/org/freedesktop/login1"};
        const sdbus::ObjectPath sessionPath{"/org/freedesktop/login1/session/auto"};

        m_pLoginProxy = sdbus::createProxy(*m_pConnection, destination, loginPath);
        m_pSessionProxy = sdbus::createProxy(*m_pConnection, destination, sessionPath);

        Debug::log(LOG, "[DBusManager] Initialized D-Bus connection. Service: {}. Login path: {}, Session path: {}",
            std::string(destination), std::string(loginPath), std::string(sessionPath));
    } catch (const sdbus::Error& e) {
        Debug::log(ERR, "[DBusManager] D-Bus connection initialization failed: {}", e.what());
    }
}

std::shared_ptr<sdbus::IConnection> DBusManager::getConnection() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pConnection;
}

std::shared_ptr<sdbus::IProxy> DBusManager::getLoginProxy() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pLoginProxy)
        initializeConnection();

    return m_pLoginProxy;
}

std::shared_ptr<sdbus::IProxy> DBusManager::getSessionProxy() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pSessionProxy)
        initializeConnection();

    return m_pSessionProxy;
}

void DBusManager::setLockedHint(bool locked) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pSessionProxy) {
        Debug::log(WARN, "[DBusManager] Cannot set locked hint: Proxy is not initialized.");
        return;
    }

    try {
        const sdbus::ServiceName interface{"org.freedesktop.login1.Session"};
        m_pSessionProxy->callMethod("SetLockedHint").onInterface(interface).withArguments(locked);

        Debug::log(LOG, "[DBusManager] Sent 'SetLockedHint({})' on {}", locked, std::string(interface));
    } catch (const sdbus::Error& e) {
        Debug::log(WARN, "[DBusManager] Failed to send 'SetLockedHint({})': {}", locked, e.what());
    }
}

void DBusManager::sendUnlockSignal() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pSessionProxy) {
        Debug::log(WARN, "[DBusManager] Unlock signal skipped: Proxy is not initialized.");
        return;
    }

    try {
        const sdbus::ServiceName interface{"org.freedesktop.login1.Session"};
        m_pSessionProxy->callMethod("Unlock").onInterface(interface);

        Debug::log(LOG, "[DBusManager] Sent 'Unlock' on {}", std::string(interface));
    } catch (const sdbus::Error& e) {
        Debug::log(WARN, "[DBusManager] Unlock signal failed: {}", e.what());
    }
}