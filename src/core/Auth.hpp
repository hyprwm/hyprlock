#pragma once

#include <hyprauth/hyprauth.hpp>
#include <array>
#include "Timer.hpp"
#include "../defines.hpp"

class CAuth {
  public:
    CAuth();
    ~CAuth();

    const std::string&           getCurrentFailText();
    const std::string&           getPromptText(Hyprauth::eAuthProvider provider);
    const std::string&           getFailText(Hyprauth::eAuthProvider provider);

    size_t                       getFailedAttempts();

    void                         resetDisplayFail();

    bool                         m_displayFail = false;
    bool                         m_pamBusy     = false;
    ASP<CTimer>                  m_resetDisplayFailTimer;

    SP<Hyprauth::IAuthenticator> m_authenticator;
    SP<Hyprauth::IAuthProvider>  m_pam;
    SP<Hyprauth::IAuthProvider>  m_fprint;

    struct SProviderInfo {
        std::string prompt = "";
        std::string fail   = "";
    };

  private:
    size_t                                                     m_failedAttempts   = 0;
    Hyprauth::eAuthProvider                                    m_lastFailProvider = Hyprauth::HYPRAUTH_PROVIDER_INVALID;

    std::unordered_map<Hyprauth::eAuthProvider, SProviderInfo> m_textInfo;
};

inline UP<CAuth> g_auth;
