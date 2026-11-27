#include "Pam.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <filesystem>
#include <unistd.h>
#include <security/pam_appl.h>
#if __has_include(<security/pam_misc.h>)
#include <security/pam_misc.h>
#endif

#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>

// Called by libpam instead of its own failure sleep.
// Skip the sleep when tearing down, so that terminate(), and the release of
// other auth methods (like the fingerprint reader), isn't held up by a failure delay.
static void failDelay(int status, unsigned int usecDelay, void* appdata_ptr) {
    const auto CONVERSATIONSTATE = (CPam::SPamConversationState*)appdata_ptr;
    if (status == PAM_SUCCESS || !CONVERSATIONSTATE || CONVERSATIONSTATE->terminateRequested || g_pHyprlock->isFadingOutOrTerminating())
        return;
    std::this_thread::sleep_for(std::chrono::microseconds(usecDelay));
}

int conv(int num_msg, const struct pam_message** msg, struct pam_response** resp, void* appdata_ptr) {
    const auto           CONVERSATIONSTATE = (CPam::SPamConversationState*)appdata_ptr;
    struct pam_response* pamReply          = (struct pam_response*)calloc(num_msg, sizeof(struct pam_response));
    bool                 initialPrompt     = true;

    *resp = nullptr;

    if (!pamReply)
        return PAM_BUF_ERR;

    const auto abortConversation = [&]() {
        for (int i = 0; i < num_msg; ++i)
            free(pamReply[i].resp);
        free(pamReply);
        return PAM_CONV_ERR;
    };

    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON: {
                const auto PROMPT        = std::string(msg[i]->msg);
                const auto PROMPTCHANGED = PROMPT != CONVERSATIONSTATE->prompt;
                Log::logger->log(Log::INFO, "PAMPROMPT: {}", PROMPT);

                // Only reuse pre-submitted input for the prompt it was collected for
                const bool FIRSTOTPTRANSACTION = !CONVERSATIONSTATE->firstPromptSeen;
                const bool REUSEPRESUBMITTED =
                    FIRSTOTPTRANSACTION && CONVERSATIONSTATE->inputPreSubmitted && (CONVERSATIONSTATE->firstPrompt.empty() || PROMPT == CONVERSATIONSTATE->preSubmittedPrompt);
                if (FIRSTOTPTRANSACTION) {
                    CONVERSATIONSTATE->firstPromptSeen = true;
                    CONVERSATIONSTATE->firstPrompt     = PROMPT;
                }
                CONVERSATIONSTATE->inputPreSubmitted = false;

                if (REUSEPRESUBMITTED) {
                    CONVERSATIONSTATE->prompt = PROMPT;
                    g_pHyprlock->enqueueForceUpdateTimers();
                } else if (initialPrompt || PROMPTCHANGED) {
                    CONVERSATIONSTATE->prompt = PROMPT;
                    g_pHyprlock->enqueueForceUpdateTimers();

                    CONVERSATIONSTATE->waitForInput();
                }

                // Needed for unlocks via SIGUSR1
                if (CONVERSATIONSTATE->terminateRequested || g_pHyprlock->isFadingOutOrTerminating())
                    return abortConversation();

                pamReply[i].resp = strdup(CONVERSATIONSTATE->input.c_str());
                initialPrompt    = false;
            } break;
            case PAM_ERROR_MSG: Log::logger->log(Log::ERR, "PAM: {}", msg[i]->msg); break;
            case PAM_TEXT_INFO: {
                Log::logger->log(Log::INFO, "PAM: {}", msg[i]->msg);
                const auto MSG = std::string_view(msg[i]->msg);
                // Targets this log from pam_faillock: https://github.com/linux-pam/linux-pam/blob/fa3295e079dbbc241906f29bde5fb71bc4172771/modules/pam_faillock/pam_faillock.c#L417
                if (MSG.contains("left to unlock")) {
                    CONVERSATIONSTATE->failText        = MSG;
                    CONVERSATIONSTATE->failTextFromPam = true;
                    // This log targets the pam_fprintd.so module (Just so $PAMPROMPT reflects what is going on)
                    // Removing this module and instead using `auth:fingerprint:enabled = true` is adviced.
                } else if (MSG.contains("Place your finger"))
                    CONVERSATIONSTATE->prompt = MSG;
            } break;
        }
    }

    *resp = pamReply;
    return PAM_SUCCESS;
}

CPam::CPam() {
    static const auto PAMMODULE = g_pConfigManager->getValue<Hyprlang::STRING>("auth:pam:module");
    m_sPamModule                = *PAMMODULE;

    if (!std::filesystem::exists(std::filesystem::path("/etc/pam.d/") / m_sPamModule)) {
        Log::logger->log(Log::ERR, R"(Pam module "/etc/pam.d/{}" does not exist! Falling back to "/etc/pam.d/su")", m_sPamModule);
        m_sPamModule = "su";
    }

    m_username = getUsernameForCurrentUid();

    m_sConversationState.waitForInput = [this]() { this->waitForInput(); };

    static const auto ENABLEFINGERPRINT = g_pConfigManager->getValue<Hyprlang::INT>("auth:fingerprint:enabled");
    m_sConversationState.deferStart     = *ENABLEFINGERPRINT;
}

CPam::~CPam() {
    terminate();
}

void CPam::init() {
    m_thread = std::thread([this]() {
        while (true) {
            resetConversation();

            if (m_sConversationState.deferStart) {
                // Show the prompt the next transaction will start with, not whatever the last one was (e.g. an OTP challenge).
                m_sConversationState.prompt = m_sConversationState.firstPrompt.empty() ? "Password: " : m_sConversationState.firstPrompt;
                g_pHyprlock->enqueueForceUpdateTimers();
                waitForInput();
                m_sConversationState.preSubmittedPrompt = m_sConversationState.prompt;
                m_sConversationState.inputPreSubmitted  = true;
                waitForInput();
                m_sConversationState.inputPreSubmitted = true;
            }

            // For grace, SIGUSR1 or fingerprint unlocks
            if (m_sConversationState.terminateRequested || g_pHyprlock->isFadingOutOrTerminating())
                return;

            const auto AUTHENTICATED = auth();

            // For SIGUSR1 unlocks
            if (m_sConversationState.terminateRequested || g_pHyprlock->isFadingOutOrTerminating())
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
    const pam_conv localConv = {.conv = conv, .appdata_ptr = (void*)&m_sConversationState};
    pam_handle_t*  handle    = nullptr;

    if (m_username.empty()) {
        m_sConversationState.failText = "Username not set";
        return false;
    }

    int ret = pam_start(m_sPamModule.c_str(), m_username.c_str(), &localConv, &handle);

    if (ret != PAM_SUCCESS) {
        m_sConversationState.failText = "pam_start failed";
        Log::logger->log(Log::ERR, "auth: pam_start failed for {}", m_sPamModule);
        return false;
    }

    pam_set_item(handle, PAM_FAIL_DELAY, (const void*)failDelay);

    ret = pam_authenticate(handle, 0);
    pam_end(handle, ret);
    handle = nullptr;

    m_sConversationState.waitingForPamAuth = false;

    if (ret != PAM_SUCCESS) {
        if (!m_sConversationState.failTextFromPam)
            m_sConversationState.failText = ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed";
        Log::logger->log(Log::ERR, "auth: {} for {}", m_sConversationState.failText, m_sPamModule);
        return false;
    }

    m_sConversationState.failText = "Successfully authenticated";
    Log::logger->log(Log::INFO, "auth: authenticated for {}", m_sPamModule);

    return true;
}

void CPam::waitForInput() {
    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);
    m_bBlockInput                          = false;
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = true;
    m_sConversationState.inputSubmittedCondition.wait(
        lk, [this] { return !m_sConversationState.inputRequested || m_sConversationState.terminateRequested || g_pHyprlock->isFadingOutOrTerminating(); });
    m_bBlockInput = true;
}

void CPam::handleInput(const std::string& input) {
    std::unique_lock<std::mutex> lk(m_sConversationState.inputMutex);

    if (!m_sConversationState.inputRequested)
        Log::logger->log(Log::ERR, "SubmitInput called, but the auth thread is not waiting for input!");

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
    {
        std::lock_guard<std::mutex> lg(m_sConversationState.inputMutex);
        m_sConversationState.terminateRequested = true;
    }

    m_sConversationState.inputSubmittedCondition.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void CPam::resetConversation() {
    m_sConversationState.input             = "";
    m_sConversationState.waitingForPamAuth = false;
    m_sConversationState.inputRequested    = false;
    m_sConversationState.failTextFromPam   = false;
    m_sConversationState.inputPreSubmitted = false;
    m_sConversationState.firstPromptSeen   = false;
    m_bBlockInput                          = false;
}
