#pragma once

#include "Auth.hpp"

#include <memory>
#include <optional>
#include <string>
#include <sdbus-c++/sdbus-c++.h>

// Face-authentication backend that talks to visaged
// (https://github.com/sovren-software/visage) over D-Bus. Calls
// Verify(username) asynchronously on the shared g_dbus connection; on a
// positive match calls g_pAuth->enqueueUnlock().
//
// Designed to coexist with the password (PAM) backend: either succeeding
// unlocks the session. A user submit (Enter) wakes a retry immediately,
// useful while visaged is recovering after hibernate-resume.
class CVisage : public IAuthImplementation {
  public:
    CVisage();
    ~CVisage() override;

    eAuthImplementations getImplType() override {
        return AUTH_IMPL_VISAGE;
    }

    void                       init() override;
    void                       handleInput(const std::string& input) override;
    bool                       checkWaiting() override;
    std::optional<std::string> getLastFailText() override;
    std::optional<std::string> getLastPrompt() override;
    void                       terminate() override;

  private:
    void scheduleVerify(int delayMs);
    void startVerify();
    void onVerifyReply(std::optional<sdbus::Error> error, bool matched);

    std::unique_ptr<sdbus::IProxy> m_proxy;
    std::unique_ptr<sdbus::IProxy> m_login;

    std::string m_username;
    std::string m_prompt;
    std::string m_readyMessage;

    int  m_retryDelayMs      = 500;
    int  m_startDelayMs      = 2000;
    int  m_consecutiveErrors = 0;

    bool m_authenticated = false;
    bool m_aborted       = false;
    bool m_sleeping      = false;
    bool m_inFlight      = false;
};
