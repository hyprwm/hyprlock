#pragma once

#include "Auth.hpp"

#include <memory>
#include <optional>
#include <string>
#include <sdbus-c++/sdbus-c++.h>

class CFingerprint : public IAuthImplementation {
  public:
    CFingerprint();

    virtual ~CFingerprint();
    virtual eAuthImplementations getImplType() {
        return AUTH_IMPL_FINGERPRINT;
    }
    virtual void                        init();
    virtual void                        handleInput(const std::string& input);
    virtual bool                        checkWaiting();
    virtual std::optional<std::string>  getLastFailText();
    virtual std::optional<std::string>  getLastPrompt();
    virtual void                        terminate();

    std::shared_ptr<sdbus::IConnection> getConnection();

  private:
    struct SDBUSState {
        std::shared_ptr<sdbus::IConnection> connection;
        std::unique_ptr<sdbus::IProxy>      login;
        std::unique_ptr<sdbus::IProxy>      device;

        bool                                abort     = false;
        bool                                done      = false;
        int                                 retries   = 0;
        bool                                sleeping  = false;
        bool                                verifying = false;
    } m_sDBUSState;

    std::string m_sFingerprintReady;
    std::string m_sFingerprintPresent;

    std::string m_sPrompt{""};
    std::string m_sFailureReason{""};

    void        handleVerifyStatus(const std::string& result, const bool done);

    bool        createDeviceProxy();
    void        claimDevice();
    void        startVerify(bool isRetry = false);
    bool        stopVerify();
    bool        releaseDevice();
};
