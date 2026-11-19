#include "SodiumPWHash.hpp"
#include "Auth.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"

#include <filesystem>
#include <hyprlang.hpp>
#include <hyprutils/path/Path.hpp>
#include <mutex>
#include <optional>
#include <sodium.h>

static std::string getSecretsConfigPath() {
    std::filesystem::path secrets_file;
    static const auto     PPWHASHSTEM = g_pConfigManager->getValue<Hyprlang::STRING>("auth:sodium:secret_file");
    const std::string     PWHASHSTEM  = *PPWHASHSTEM;
    std::filesystem::path dir         = g_pConfigManager->configCurrentPath;
    dir                               = dir.parent_path();

    RASSERT(!PWHASHSTEM.empty(), "[SodiumAuth] auth:sodium:secret_file must be set to a non-empty value");

    if (PWHASHSTEM.contains('/')) {
        if (PWHASHSTEM.starts_with('/'))
            // An absolute path
            secrets_file = PWHASHSTEM;
        else
            // A relative path to main config file
            secrets_file = dir / PWHASHSTEM;
    } else {
        // A stem
        secrets_file = dir / PWHASHSTEM;
        secrets_file += ".conf";
    }

    RASSERT(std::filesystem::exists(secrets_file), "[SodiumAuth] Failed to find {}. Use \"hyprlock-pwhash\" to generate it!", secrets_file.c_str());

    // check permissions
    using std::filesystem::perms;
    const auto PERMS = std::filesystem::status(secrets_file).permissions();
    if ((PERMS & perms::group_read) != perms::none || (PERMS & perms::group_write) != perms::none || (PERMS & perms::others_read) != perms::none ||
        (PERMS & perms::others_write) != perms::none) {
        RASSERT(false, "[SodiumAuth] {} has insecure permissions", secrets_file.c_str());
    }
    return secrets_file;
}

CSodiumPWHash::CSodiumPWHash() : m_config(getSecretsConfigPath().c_str(), {}) {
    m_config.addConfigValue("hash", Hyprlang::STRING{""});
    m_config.commence();
    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "[SodiumAuth] Error in configuration:\n{}\nProceeding", result.getError());
}

CSodiumPWHash::~CSodiumPWHash() {
    ;
}

void* const* CSodiumPWHash::getConfigValuePtr(const std::string& name) {
    return m_config.getConfigValuePtr(name.c_str())->getDataStaticPtr();
}

void CSodiumPWHash::init() {
    RASSERT(sodium_init() >= 0, "[SodiumAuth] Failed to initialise libsodium");
    m_thread = std::thread([this]() {
        while (true) {
            m_sCheckerState.prompt = "Password: ";
            waitForInput();

            // For grace or SIGUSR1 unlocks
            if (g_pHyprlock->isUnlocked())
                return;

            const auto AUTHENTICATED = auth();

            // For SIGUSR1 unlocks
            if (g_pHyprlock->isUnlocked())
                return;

            if (!AUTHENTICATED)
                g_pAuth->enqueueFail(m_sCheckerState.failText, AUTH_IMPL_SODIUMPWHASH);
            else {
                g_pAuth->enqueueUnlock();
                break;
            }
        }
    });
}

void CSodiumPWHash::waitForInput() {
    std::unique_lock<std::mutex> lk(m_sCheckerState.inputMutex);
    m_bBlockInput         = false;
    m_sCheckerState.state = SODIUMHASH_INPUT;
    m_sCheckerState.inputSubmittedCondition.wait(lk, [this]() { return (m_sCheckerState.state != SODIUMHASH_INPUT) || g_pHyprlock->m_bTerminate; });
    m_bBlockInput = true;
}

bool CSodiumPWHash::auth() {
    static auto const PPWHASH = (Hyprlang::STRING*)getConfigValuePtr("hash");
    const std::string PWHASH  = *PPWHASH;
    bool              rv;

    if (PWHASH.empty() || PWHASH.size() > crypto_pwhash_STRBYTES) {
        m_sCheckerState.failText = "Invalid hash. Check config";
        Debug::log(ERR, "[SodiumAuth] Invalid password hash set in configuration");
        rv = false;
    } else if (crypto_pwhash_str_verify(PWHASH.c_str(), m_sCheckerState.input.c_str(), m_sCheckerState.input.length()) == 0) {
        rv = true;
    } else {
        m_sCheckerState.failText = "Failed to authenticate";
        Debug::log(LOG, "[SodiumAuth] Failed to authenticate");
        rv = false;
    }
    m_sCheckerState.input.clear();
    m_sCheckerState.state = SODIUMHASH_IDLE;
    return rv;
}

void CSodiumPWHash::handleInput(const std::string& input) {
    std::unique_lock<std::mutex> lk(m_sCheckerState.inputMutex);

    if (m_sCheckerState.state != SODIUMHASH_INPUT)
        Debug::log(ERR, "SubmitInput called, but auth thread is not waiting for input!");

    m_sCheckerState.input = input;
    m_sCheckerState.state = SODIUMHASH_AUTH;
    m_sCheckerState.inputSubmittedCondition.notify_all();
}

bool CSodiumPWHash::checkWaiting() {
    return m_bBlockInput || (m_sCheckerState.state == SODIUMHASH_AUTH);
}

std::optional<std::string> CSodiumPWHash::getLastFailText() {
    return m_sCheckerState.failText.empty() ? std::nullopt : std::optional(m_sCheckerState.failText);
}

std::optional<std::string> CSodiumPWHash::getLastPrompt() {
    return m_sCheckerState.prompt.empty() ? std::nullopt : std::optional(m_sCheckerState.prompt);
}

void CSodiumPWHash::terminate() {
    m_sCheckerState.inputSubmittedCondition.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}
