#include "Auth.hpp"
#include "Pam.hpp"
#include "Fingerprint.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/hyprlock.hpp"
#include "src/helpers/Log.hpp"

#include <hyprlang.hpp>
#include <memory>

CAuth::CAuth() {
    m_vImpls.push_back(std::make_shared<CPam>());
    static auto* const PENABLEFINGERPRINT = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:enable_fingerprint");
    if (**PENABLEFINGERPRINT)
        m_vImpls.push_back(std::make_shared<CFingerprint>());
}

void CAuth::start() {
    for (const auto& i : m_vImpls) {
        i->init();
    }
}

bool CAuth::isAuthenticated() {
    for (const auto& i : m_vImpls) {
        if (i->isAuthenticated())
            return true;
    }

    return false;
}

void CAuth::submitInput(const std::string& input) {
    for (const auto& i : m_vImpls) {
        i->handleInput(input);
    }
}

std::optional<std::string> CAuth::getLastFailText() {
    for (const auto& i : m_vImpls) {
        const auto FAIL = i->getLastFailText();
        if (FAIL.has_value())
            return FAIL;
    }

    return std::nullopt;
}

std::optional<std::string> CAuth::getLastPrompt() {
    for (const auto& i : m_vImpls) {
        const auto PROMPT = i->getLastPrompt();
        if (PROMPT.has_value())
            return PROMPT;
    }

    return std::nullopt;
}

bool CAuth::checkWaiting() {
    for (const auto& i : m_vImpls) {
        if (i->checkWaiting())
            return true;
    }

    return false;
}

std::shared_ptr<IAuthImplementation> CAuth::getImpl(const eAuthImplementations implType) {
    for (const auto& i : m_vImpls) {
        if (i->getImplType() == implType)
            return i;
    }

    return nullptr;
}

void CAuth::terminate() {
    for (const auto& i : m_vImpls) {
        i->terminate();
    }
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    // check result
    if (g_pAuth->isAuthenticated()) {
        g_pHyprlock->unlock();
    } else {
        g_pHyprlock->clearPasswordBuffer();
        g_pAuth->m_iFailedAttempts += 1;
        Debug::log(LOG, "Failed attempts: {}", g_pAuth->m_iFailedAttempts);

        g_pAuth->m_bDisplayFailText = true;
        g_pHyprlock->enqueueForceUpdateTimers();

        g_pHyprlock->renderAllOutputs();
    }
}

void CAuth::enqueueCheckAuthenticated() {
    g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
}
