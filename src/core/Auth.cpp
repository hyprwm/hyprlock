#include "Auth.hpp"
#include "hyprlock.hpp"

#include "../config/ConfigManager.hpp"
#include "../helpers/Log.hpp"

using namespace Hyprauth;

static void displayFailTimeoutCallback(ASP<CTimer> self, void* data) {
    if (g_auth->m_displayFail) {
        g_auth->m_displayFail = false;
        g_pHyprlock->renderAllOutputs();
    }
}

CAuth::CAuth() {
    m_authenticator = IAuthenticator::create(SAuthenticatorCreationData{});

    static const auto ENABLEPAM         = g_pConfigManager->getValue<Hyprlang::INT>("auth:pam:enabled");
    static const auto ENABLEFINGERPRINT = g_pConfigManager->getValue<Hyprlang::INT>("auth:fingerprint:enabled");
    static const auto FAILTIMEOUT       = g_pConfigManager->getValue<Hyprlang::INT>("general:fail_timeout");

    RASSERT(*ENABLEPAM || *ENABLEFINGERPRINT, "At least one authentication method must be enabled!");

    if (*ENABLEPAM) {
        SPamCreationData pamData;
        pamData.module          = "hyprlock"; // hyprauth will fall back to su in case it doesn't exist
        pamData.extendUserCreds = true;
        m_pam                   = createPamProvider(pamData);
        if (m_pam)
            m_authenticator->addProvider(m_pam);
    }

    if (*ENABLEFINGERPRINT) {
        m_fprint = createFprintProvider(SFprintCreationData{});
        if (m_fprint)
            m_authenticator->addProvider(m_fprint);
    }

    m_textInfo[HYPRAUTH_PROVIDER_INVALID] = SProviderInfo{};

    m_authenticator->m_events.prompt.listenStatic([this](IAuthenticator::SAuthPromptData data) {
        if (m_textInfo.contains(data.from))
            m_textInfo[data.from] = SProviderInfo{.prompt = data.promptText, .fail = ""};
        else
            m_textInfo[data.from].prompt = data.promptText;

        Debug::log(INFO, "Prompt text: {}", data.promptText);
        g_pHyprlock->renderAllOutputs();
    });

    m_authenticator->m_events.fail.listenStatic([this](IAuthenticator::SAuthFailData data) {
        if (m_textInfo.contains(data.from))
            m_textInfo[data.from] = SProviderInfo{.prompt = "", .fail = data.failText};
        else
            m_textInfo[data.from].fail = data.failText;

        m_lastFailProvider = data.from;
        m_failedAttempts++;
        m_displayFail = true;

        if (m_resetDisplayFailTimer) {
            m_resetDisplayFailTimer->cancel();
            m_resetDisplayFailTimer.reset();
        }

        Debug::log(WARN, "Fail text: {}, attempts: {}", data.failText, m_failedAttempts);
        m_resetDisplayFailTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(*FAILTIMEOUT), displayFailTimeoutCallback, nullptr);

        g_pHyprlock->clearPasswordBuffer();
    });

    m_authenticator->m_events.busy.listenStatic([this](IAuthenticator::SBusyData data) {
        if (data.from == HYPRAUTH_PROVIDER_PAM) {
            Debug::log(LOG, "Pam busy: {}", data.busy);
            m_pamBusy = data.busy;
        }
    });

    m_authenticator->m_events.success.listenStatic([this](eAuthProvider tok) {
        Debug::log(INFO, "Success!");
        g_pHyprlock->unlock();
    });
}

CAuth::~CAuth() {}

const std::string& CAuth::getCurrentFailText() {
    return m_textInfo[m_lastFailProvider].fail;
}

const std::string& CAuth::getPromptText(eAuthProvider provider) {
    return m_textInfo[provider].prompt;
}

const std::string& CAuth::getFailText(eAuthProvider provider) {
    return m_textInfo[provider].fail;
}

size_t CAuth::getFailedAttempts() {
    return m_failedAttempts;
}

void CAuth::resetDisplayFail() {
    displayFailTimeoutCallback(nullptr, nullptr);
}
