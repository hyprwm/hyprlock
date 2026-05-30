#include "Visage.hpp"
#include "DBus.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"

#include <chrono>
#include <pwd.h>
#include <unistd.h>

// Per-call timeout on the Verify D-Bus method. visaged opens the camera, runs
// inference, and replies (typically ~500-1500ms). After hibernate or on a
// stale camera fd it can take far longer; cap so a stuck visaged can't
// deadlock the dispatch.
constexpr int VERIFY_TIMEOUT_MS = 30'000;

// Backoff parameters when Verify keeps failing (visaged unavailable,
// restarting, etc). delay = min(retry_delay << min(errors, MAX_BACKOFF_SHIFT),
// MAX_BACKOFF_MS).
constexpr int MAX_BACKOFF_MS    = 5'000;
constexpr int MAX_BACKOFF_SHIFT = 4;

static const auto VISAGE_SERVICE   = sdbus::ServiceName{"org.freedesktop.Visage1"};
static const auto VISAGE_OBJECT    = sdbus::ObjectPath{"/org/freedesktop/Visage1"};
static const auto VISAGE_INTERFACE = sdbus::InterfaceName{"org.freedesktop.Visage1"};
static const auto LOGIN_MANAGER    = sdbus::ServiceName{"org.freedesktop.login1.Manager"};

CVisage::CVisage() {
    static const auto READYMSG = g_pConfigManager->getValue<Hyprlang::STRING>("auth:visage:ready_message");
    static const auto RETRYDLY = g_pConfigManager->getValue<Hyprlang::INT>("auth:visage:retry_delay");
    static const auto STARTDLY = g_pConfigManager->getValue<Hyprlang::INT>("auth:visage:start_delay");
    m_readyMessage = *READYMSG;
    m_retryDelayMs = *RETRYDLY;
    m_startDelayMs = *STARTDLY;

    if (auto* pw = getpwuid(getuid()); pw && pw->pw_name)
        m_username = pw->pw_name;
}

CVisage::~CVisage() = default;

void CVisage::init() {
    if (m_username.empty()) {
        Log::logger->log(Log::ERR, "visage: could not resolve current username; disabled");
        return;
    }

    if (!initDBus()) {
        Log::logger->log(Log::ERR, "visage: no dbus connection; disabled");
        return;
    }

    try {
        m_proxy = sdbus::createProxy(*g_dbus, VISAGE_SERVICE, VISAGE_OBJECT);
        m_login = sdbus::createProxy(*g_dbus, sdbus::ServiceName{"org.freedesktop.login1"}, sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (const sdbus::Error& e) {
        Log::logger->log(Log::ERR, "visage: failed to create proxies ({})", e.what());
        m_proxy.reset();
        m_login.reset();
        return;
    }

    m_login->uponSignal("PrepareForSleep").onInterface(LOGIN_MANAGER).call([this](bool start) {
        Log::logger->log(Log::INFO, "visage: PrepareForSleep (start: {})", start);
        m_sleeping = start;
        if (!start && !m_inFlight && !m_authenticated && !m_aborted)
            scheduleVerify(m_retryDelayMs);
    });

    m_prompt = m_readyMessage;
    scheduleVerify(m_startDelayMs);
}

void CVisage::handleInput(const std::string& /*input*/) {
    // Nudge a fresh attempt — useful while visaged is recovering after
    // hibernate-resume and the user hits Enter to retry.
    if (m_aborted || m_authenticated || m_inFlight || m_sleeping || !m_proxy)
        return;
    Log::logger->log(Log::INFO, "visage: nudge - retry triggered by user input");
    startVerify();
}

bool CVisage::checkWaiting() {
    return false;
}

std::optional<std::string> CVisage::getLastFailText() {
    return std::nullopt;
}

std::optional<std::string> CVisage::getLastPrompt() {
    if (!m_prompt.empty())
        return std::optional(m_prompt);
    return std::nullopt;
}

void CVisage::terminate() {
    m_aborted = true;
}

void CVisage::scheduleVerify(int delayMs) {
    if (m_aborted || m_authenticated)
        return;
    g_pHyprlock->addTimer(
        std::chrono::milliseconds(delayMs), [](ASP<CTimer>, void* data) { ((CVisage*)data)->startVerify(); }, this);
}

void CVisage::startVerify() {
    if (m_aborted || m_authenticated || m_sleeping || m_inFlight || !m_proxy)
        return;

    m_inFlight = true;
    m_proxy->callMethodAsync("Verify")
        .onInterface(VISAGE_INTERFACE)
        .withTimeout(std::chrono::milliseconds(VERIFY_TIMEOUT_MS))
        .withArguments(m_username)
        .uponReplyInvoke([this](std::optional<sdbus::Error> e, bool matched) { onVerifyReply(std::move(e), matched); });
}

void CVisage::onVerifyReply(std::optional<sdbus::Error> error, bool matched) {
    m_inFlight = false;
    if (m_aborted || m_authenticated)
        return;

    if (error) {
        ++m_consecutiveErrors;
        // visaged can take 30-90s to come back after hibernate-resume; log
        // first + every 10th to avoid spamming.
        if (m_consecutiveErrors == 1 || m_consecutiveErrors % 10 == 0)
            Log::logger->log(Log::WARN, "visage: Verify failed ({}x): {}", m_consecutiveErrors, error->what());
        const int shift   = std::min(m_consecutiveErrors, MAX_BACKOFF_SHIFT);
        const int backoff = std::min(m_retryDelayMs * (1 << shift), MAX_BACKOFF_MS);
        scheduleVerify(backoff);
        return;
    }

    m_consecutiveErrors = 0;

    if (matched) {
        Log::logger->log(Log::INFO, "visage: face matched - unlocking");
        m_authenticated = true;
        g_pAuth->enqueueUnlock();
        return;
    }

    scheduleVerify(m_retryDelayMs);
}
