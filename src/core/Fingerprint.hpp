#pragma once

#include "hyprlock.hpp"

#include <memory>
#include <optional>
#include <string>
#include <sdbus-c++/sdbus-c++.h>

class CFingerprint {
  public:
    CFingerprint();

    std::shared_ptr<sdbus::IConnection> start();
    bool                                isAuthenticated();
    std::optional<std::string>          getLastMessage();
    void                                terminate();

  private:
    struct SDBUSState {
        std::string                         message = "";

        std::shared_ptr<sdbus::IConnection> connection;
        std::unique_ptr<sdbus::IProxy>      login;
        std::unique_ptr<sdbus::IProxy>      device;
        sdbus::UnixFd                       inhibitLock;

        bool                                abort    = false;
        bool                                done     = false;
        int                                 retries  = 0;
        bool                                sleeping = false;
    } m_sDBUSState;

    std::string                        m_sFingerprintReady;
    std::string                        m_sFingerprintPresent;
    bool                               m_bAuthenticated = false;
    bool                               m_bEnabled       = false;

    void                               handleVerifyStatus(const std::string& result, const bool done);

    void                               registerSleepHandler();
    void                               inhibitSleep();

    bool                               createDeviceProxy();
    bool                               claimDevice();
    bool                               startVerify(bool updateMessage = true);
    bool                               stopVerify();
    bool                               releaseDevice();
};

inline std::unique_ptr<CFingerprint> g_pFingerprint;
