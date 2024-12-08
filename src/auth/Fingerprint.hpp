#pragma once

#include "Auth.hpp"

#include <memory>
#include <optional>
#include <string>
#include <sdbus-c++/sdbus-c++.h>

class CFingerprint : public IAuthImplementation {
  public:
    CFingerprint();

    ~CFingerprint() override;
    eAuthImplementations getImplType() override {
        return AUTH_IMPL_FINGERPRINT;
    }
    void                                init() override;
    void                                handleInput(const std::string& input) override;
    bool                                isAuthenticated() override;
    bool                                checkWaiting() override;
    std::optional<std::string>          getLastFailText() override;
    std::optional<std::string>          getLastPrompt() override;
    void                                terminate() override;

    std::shared_ptr<sdbus::IConnection> getConnection();

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

    std::string m_sFingerprintReady;
    std::string m_sFingerprintPresent;
    bool        m_bAuthenticated = false;

    void        handleVerifyStatus(const std::string& result, const bool done);

    void        inhibitSleep();

    bool        createDeviceProxy();
    void        claimDevice();
    void        startVerify(bool isRetry = false);
    bool        stopVerify();
    bool        releaseDevice();
};
