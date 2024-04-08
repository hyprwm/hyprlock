#include "Auth.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "src/config/ConfigManager.hpp"

#include <filesystem>
#include <unistd.h>
#include <pwd.h>
#include <security/pam_appl.h>
#if __has_include(<security/pam_misc.h>)
#include <security/pam_misc.h>
#endif

#include <cstring>
#include <thread>

int conv(int num_msg, const struct pam_message** msg, struct pam_response** resp, void* appdata_ptr) {
    const auto           VERIFICATIONSTATE = (CAuth::SPamConversationState*)appdata_ptr;
    struct pam_response* pam_reply         = (struct pam_response*)calloc(num_msg, sizeof(struct pam_response));

    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON: {
                g_pAuth->setPrompt(msg[i]->msg);
                // Some pam configurations ask for the password twice for whatever reason (Fedora su for example)
                // When the prompt is the same as the last one, I guess our answer can be the same.
                Debug::log(LOG, "PAM_PROMPT: {}", msg[i]->msg);
                if (VERIFICATIONSTATE->prompt != VERIFICATIONSTATE->lastPrompt)
                    g_pAuth->waitForInput();

                // Needed for unlocks via SIGUSR1
                if (g_pHyprlock->m_bTerminate)
                    return PAM_CONV_ERR;

                pam_reply[i].resp = strdup(VERIFICATIONSTATE->input.c_str());
                break;
            }
            case PAM_ERROR_MSG: Debug::log(ERR, "PAM: {}", msg[i]->msg); break;
            case PAM_TEXT_INFO: Debug::log(LOG, "PAM: {}", msg[i]->msg); break;
        }
    }

    *resp = pam_reply;
    return PAM_SUCCESS;
}

CAuth::CAuth() {
    static auto* const PPAMMODULE = (Hyprlang::STRING*)(g_pConfigManager->getValuePtr("general:pam_module"));
    m_sPamModule                  = *PPAMMODULE;

    if (!std::filesystem::exists(std::filesystem::path("/etc/pam.d/") / m_sPamModule)) {
        Debug::log(ERR, "Pam module \"{}\" not found! Falling back to \"su\"", m_sPamModule);
        m_sPamModule = "su";
    }
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->onPasswordCheckTimer();
}

void CAuth::start() {
    std::thread([this]() {
        resetConversation();
        auth(m_sPamModule);

        g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
    }).detach();
}

bool CAuth::auth(std::string pam_module) {
    const pam_conv localConv   = {conv, (void*)&m_sConversationState};
    pam_handle_t*  handle      = NULL;
    auto           uidPassword = getpwuid(getuid());

    int            ret = pam_start(pam_module.c_str(), uidPassword->pw_name, &localConv, &handle);

    if (ret != PAM_SUCCESS) {
        m_sConversationState.success = false;
        m_sFailText                  = "pam_start failed";
        Debug::log(ERR, "auth: pam_start failed for {}", pam_module);
        return false;
    }

    ret = pam_authenticate(handle, 0);

    m_sConversationState.waitingForPamAuth = false;

    if (ret != PAM_SUCCESS) {
        m_sConversationState.success = false;
        m_sFailText                  = ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed";
        Debug::log(ERR, "auth: {} for {}", m_sFailText, pam_module);
        return false;
    }

    ret = pam_end(handle, ret);

    m_sConversationState.success = true;
    m_sFailText                  = "Successfully authenticated";
    Debug::log(LOG, "auth: authenticated for {}", pam_module);

    return true;
}

bool CAuth::didAuthSucceed() {
    return m_sConversationState.success;
}

// clearing the input must be done from the main thread!
static void onWaitForInputTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->clearPasswordBuffer();
}

void CAuth::waitForInput() {
    if (!m_sConversationState.lastPrompt.empty())
        g_pHyprlock->addTimer(std::chrono::milliseconds(1), onWaitForInputTimerCallback, nullptr);

    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);
    m_bBlockInput                          = false;
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = true;
    m_sConversationState.inputSubmittedCondition.wait(lk, [this] { return !m_sConversationState.inputRequested || g_pHyprlock->m_bTerminate; });
    m_bBlockInput = true;
}

void CAuth::submitInput(std::string input) {
    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);

    if (!m_sConversationState.inputRequested)
        Debug::log(ERR, "SubmitInput called, but the auth thread is not waiting for input!");

    m_sConversationState.input             = input;
    m_sConversationState.inputRequested    = false;
    m_sConversationState.waitingForPamAuth = true;
    m_sConversationState.inputSubmittedCondition.notify_all();
}

CAuth::SFeedback CAuth::getFeedback() {
    if (!m_sFailText.empty()) {
        return SFeedback{m_sFailText, true};
    } else if (!m_sConversationState.prompt.empty()) {
        return SFeedback{m_sConversationState.prompt, false};
    } else {
        return SFeedback{"Starting Auth...", false};
    }
}

void CAuth::setPrompt(const char* prompt) {
    m_sConversationState.lastPrompt = m_sConversationState.prompt;
    m_sConversationState.prompt     = prompt;
}

void CAuth::clearFailText() {
    m_sFailText = "";
}

bool CAuth::checkWaiting() {
    return m_bBlockInput || m_sConversationState.waitingForPamAuth;
}

void CAuth::terminate() {
    m_sConversationState.inputSubmittedCondition.notify_all();
}

void CAuth::resetConversation() {
    m_sConversationState.input             = "";
    m_sConversationState.prompt            = "";
    m_sConversationState.lastPrompt        = "";
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = false;
    m_sConversationState.success           = false;
}
