#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>

class DBusManager {
public:
    static DBusManager& getInstance();

    std::shared_ptr<sdbus::IConnection> getConnection();
    std::shared_ptr<sdbus::IProxy>      getLoginProxy();
    std::shared_ptr<sdbus::IProxy>      getSessionProxy();

    void setLockedHint(bool locked);
    void sendUnlockSignal();

private:
    DBusManager();
    ~DBusManager();

    void initializeConnection();

    std::shared_ptr<sdbus::IConnection> m_pConnection;
    std::shared_ptr<sdbus::IProxy>      m_pLoginProxy;
    std::shared_ptr<sdbus::IProxy>      m_pSessionProxy;

    std::mutex                          m_mutex;
};
