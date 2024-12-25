#include "SodiumPWHash.hpp"

#include "../helpers/Log.hpp"
#include "../core/hyprlock.hpp"

#include <filesystem>
#include <hyprutils/path/Path.hpp>
#include <sodium.h>

static std::string getSecretsConfigPath() {
    static const auto [PWHASHPATH, DOTDIR] = Hyprutils::Path::findConfig("hyprlock_pwhash");
    (void)DOTDIR;

    RASSERT(PWHASHPATH.has_value(), "[SodiumAuth] Failed to find hyprlock_pwhash.conf. Please use \"hyprlock-setpwhash\" to generate it!");
    // check permissions
    using std::filesystem::perms;
    const auto PERMS = std::filesystem::status(PWHASHPATH.value()).permissions();
    if ((PERMS & perms::group_read) != perms::none || (PERMS & perms::group_write) != perms::none || (PERMS & perms::others_read) != perms::none ||
        (PERMS & perms::others_write) != perms::none) {
        RASSERT(false, "[SodiumAuth] hyprlock_pwhash.conf has insecure permissions");
    }
    return PWHASHPATH.value();
}

void* const* CSodiumPWHash::getConfigValuePtr(const std::string& name) {
    return m_config.getConfigValuePtr(name.c_str())->getDataStaticPtr();
}

CSodiumPWHash::CSodiumPWHash() : m_config(getSecretsConfigPath().c_str(), {}) {
    m_config.addConfigValue("pw_hash", Hyprlang::STRING{""});
    m_config.commence();
    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());

    m_checkerThread = std::thread([this]() { checkerLoop(); });
}

CSodiumPWHash::~CSodiumPWHash() {
    ;
}

void CSodiumPWHash::init() {
    RASSERT(sodium_init() >= 0, "Failed to initialize libsodium");
}

void CSodiumPWHash::handleInput(const std::string& input) {
    std::lock_guard<std::mutex> lk(m_sCheckerState.requestMutex);

    m_sCheckerState.input     = input;
    m_sCheckerState.requested = true;

    m_sCheckerState.requestCV.notify_all();
}

bool CSodiumPWHash::checkWaiting() {
    return m_sCheckerState.requested;
}

std::optional<std::string> CSodiumPWHash::getLastFailText() {
    return m_sCheckerState.failText.empty() ? std::nullopt : std::optional<std::string>(m_sCheckerState.failText);
}

std::optional<std::string> CSodiumPWHash::getLastPrompt() {
    return "Password: ";
}

void CSodiumPWHash::terminate() {
    m_sCheckerState.requestCV.notify_all();
    if (m_checkerThread.joinable())
        m_checkerThread.join();
}

void CSodiumPWHash::checkerLoop() {
    static auto* const PPWHASH = (Hyprlang::STRING*)getConfigValuePtr("pw_hash");
    const auto         PWHASH  = std::string(*PPWHASH);

    while (true) {
        std::unique_lock<std::mutex> lk(m_sCheckerState.requestMutex);
        m_sCheckerState.requestCV.wait(lk, [this]() { return m_sCheckerState.requested || g_pHyprlock->m_bTerminate; });

        if (g_pHyprlock->isUnlocked())
            return;

        if (PWHASH.empty() || PWHASH.size() > crypto_pwhash_STRBYTES) {
            m_sCheckerState.failText = "Invalid password hash";
            Debug::log(ERR, "[SodiumAuth] Invalid password hash set in secrets.conf");
            g_pAuth->enqueueFail();
        } else if (crypto_pwhash_str_verify(PWHASH.c_str(), m_sCheckerState.input.c_str(), m_sCheckerState.input.length()) == 0) {
            g_pAuth->enqueueUnlock();
        } else {
            g_pAuth->enqueueFail();
            m_sCheckerState.failText = "Failed to authenticate";
            Debug::log(LOG, "[SodiumAuth] Failed to authenticate");
        }

        m_sCheckerState.input.clear();
        m_sCheckerState.requested = false;
    }
}
