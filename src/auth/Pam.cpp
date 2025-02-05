#include "Pam.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"

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
    const auto           CONVERSATIONSTATE = (CPam::SPamConversationState*)appdata_ptr;
    struct pam_response* pamReply          = (struct pam_response*)calloc(num_msg, sizeof(struct pam_response));
    bool                 initialPrompt     = true;

    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON: {
                const auto PROMPT        = std::string(msg[i]->msg);
                const auto PROMPTCHANGED = PROMPT != CONVERSATIONSTATE->prompt;
                Debug::log(LOG, "PAM_PROMPT: {}", PROMPT);

                if (PROMPTCHANGED)
                    g_pHyprlock->enqueueForceUpdateTimers();

                // Some pam configurations ask for the password twice for whatever reason (Fedora su for example)
                // When the prompt is the same as the last one, I guess our answer can be the same.
                if (!initialPrompt && PROMPTCHANGED) {
                    CONVERSATIONSTATE->prompt = PROMPT;
                    CONVERSATIONSTATE->waitForInput();
                }

                // Needed for unlocks via SIGUSR1
                if (g_pHyprlock->isUnlocked())
                    return PAM_CONV_ERR;

                pamReply[i].resp = strdup(CONVERSATIONSTATE->input.c_str());
                initialPrompt    = false;
            } break;
            case PAM_ERROR_MSG: Debug::log(ERR, "PAM: {}", msg[i]->msg); break;
            case PAM_TEXT_INFO:
                Debug::log(LOG, "PAM: {}", msg[i]->msg);
                // Targets this log from pam_faillock: https://github.com/linux-pam/linux-pam/blob/fa3295e079dbbc241906f29bde5fb71bc4172771/modules/pam_faillock/pam_faillock.c#L417
                if (const auto MSG = std::string(msg[i]->msg); MSG.contains("left to unlock")) {
                    CONVERSATIONSTATE->failText        = MSG;
                    CONVERSATIONSTATE->failTextFromPam = true;
                }
                break;
        }
    }

    *resp = pamReply;
    return PAM_SUCCESS;
}

CPam::CPam() {
    static const auto PAMMODULE = g_pConfigManager->getValue<Hyprlang::STRING>("auth:pam:module");
    m_sPamModule                = *PAMMODULE;

    if (!std::filesystem::exists(std::filesystem::path("/etc/pam.d/") / m_sPamModule)) {
        Debug::log(ERR, R"(Pam module "/etc/pam.d/{}" does not exist! Falling back to "/etc/pam.d/su")", m_sPamModule);
        m_sPamModule = "su";
    }

    m_sConversationState.waitForInput = [this]() { this->waitForInput(); };
}

CPam::~CPam() {
    ;
}

void CPam::init() {
    m_thread = std::thread([this]() {
        while (true) {
            resetConversation();

            // Initial input
            m_sConversationState.prompt = "Password: ";
            waitForInput();

            // For grace or SIGUSR1 unlocks
            if (g_pHyprlock->isUnlocked())
                return;

            const auto AUTHENTICATED = auth();

            // For SIGUSR1 unlocks
            if (g_pHyprlock->isUnlocked())
                return;

            if (!AUTHENTICATED)
                g_pAuth->enqueueFail(m_sConversationState.failText, AUTH_IMPL_PAM);
            else {
                g_pAuth->enqueueUnlock();
                return;
            }
        }
    });
}

bool CPam::auth() {
    const pam_conv localConv   = {.conv = conv, .appdata_ptr = (void*)&m_sConversationState};
    pam_handle_t*  handle      = nullptr;
    auto           uidPassword = getpwuid(getuid());
    RASSERT(uidPassword && uidPassword->pw_name, "Failed to get username (getpwuid)");

    int ret = pam_start(m_sPamModule.c_str(), uidPassword->pw_name, &localConv, &handle);

    if (ret != PAM_SUCCESS) {
        m_sConversationState.failText = "pam_start failed";
        Debug::log(ERR, "auth: pam_start failed for {}", m_sPamModule);
        return false;
    }

    ret = pam_authenticate(handle, 0);
    pam_end(handle, ret);
    handle = nullptr;

    m_sConversationState.waitingForPamAuth = false;

    if (ret != PAM_SUCCESS) {
        if (!m_sConversationState.failTextFromPam)
            m_sConversationState.failText = ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed";
        Debug::log(ERR, "auth: {} for {}", m_sConversationState.failText, m_sPamModule);
        return false;
    }

    m_sConversationState.failText = "Successfully authenticated";
    Debug::log(LOG, "auth: authenticated for {}", m_sPamModule);

    return true;
}

// clearing the input must be done from the main thread
static void clearInputTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->clearPasswordBuffer();
}

void CPam::waitForInput() {
    g_pHyprlock->addTimer(std::chrono::milliseconds(1), clearInputTimerCallback, nullptr);

    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);
    m_bBlockInput                          = false;
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = true;
    m_sConversationState.inputSubmittedCondition.wait(lk, [this] { return !m_sConversationState.inputRequested || g_pHyprlock->m_bTerminate; });
    m_bBlockInput = true;
}

void CPam::handleInput(const std::string& input) {
    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);

    if (!m_sConversationState.inputRequested)
        Debug::log(ERR, "SubmitInput called, but the auth thread is not waiting for input!");

    m_sConversationState.input             = input;
    m_sConversationState.inputRequested    = false;
    m_sConversationState.waitingForPamAuth = true;
    m_sConversationState.inputSubmittedCondition.notify_all();
}

std::optional<std::string> CPam::getLastFailText() {
    return m_sConversationState.failText.empty() ? std::nullopt : std::optional(m_sConversationState.failText);
}

std::optional<std::string> CPam::getLastPrompt() {
    return m_sConversationState.prompt.empty() ? std::nullopt : std::optional(m_sConversationState.prompt);
}

bool CPam::checkWaiting() {
    return m_bBlockInput || m_sConversationState.waitingForPamAuth;
}

void CPam::terminate() {
    m_sConversationState.inputSubmittedCondition.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void CPam::resetConversation() {
    m_sConversationState.input             = "";
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = false;
    m_sConversationState.failTextFromPam   = false;
}
