#include "Fingerprint.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"

#include <memory>
#include <unistd.h>
#include <pwd.h>

#include <cstring>

static const auto FPRINT        = sdbus::ServiceName{"net.reactivated.Fprint"};
static const auto DEVICE        = sdbus::ServiceName{"net.reactivated.Fprint.Device"};
static const auto MANAGER       = sdbus::ServiceName{"net.reactivated.Fprint.Manager"};
static const auto LOGIN_MANAGER = sdbus::ServiceName{"org.freedesktop.login1.Manager"};

enum MatchResult {
    MATCH_INVALID = 0,
    MATCH_NO_MATCH,
    MATCH_MATCHED,
    MATCH_RETRY,
    MATCH_SWIPE_TOO_SHORT,
    MATCH_FINGER_NOT_CENTERED,
    MATCH_REMOVE_AND_RETRY,
    MATCH_DISCONNECTED,
    MATCH_UNKNOWN_ERROR,
};

static std::map<std::string, MatchResult> s_mapStringToTestType = {{"verify-no-match", MATCH_NO_MATCH},
                                                                   {"verify-match", MATCH_MATCHED},
                                                                   {"verify-retry-scan", MATCH_RETRY},
                                                                   {"verify-swipe-too-short", MATCH_SWIPE_TOO_SHORT},
                                                                   {"verify-finger-not-centered", MATCH_FINGER_NOT_CENTERED},
                                                                   {"verify-remove-and-retry", MATCH_REMOVE_AND_RETRY},
                                                                   {"verify-disconnected", MATCH_DISCONNECTED},
                                                                   {"verify-unknown-error", MATCH_UNKNOWN_ERROR}};

CFingerprint::CFingerprint() {
    static const auto FINGERPRINTREADY   = g_pConfigManager->getValue<Hyprlang::STRING>("auth:fingerprint:ready_message");
    m_sFingerprintReady                  = *FINGERPRINTREADY;
    static const auto FINGERPRINTPRESENT = g_pConfigManager->getValue<Hyprlang::STRING>("auth:fingerprint:present_message");
    m_sFingerprintPresent                = *FINGERPRINTPRESENT;
}

CFingerprint::~CFingerprint() {
    ;
}

void CFingerprint::init() {
    try {
        m_sDBUSState.connection = sdbus::createSystemBusConnection();
        m_sDBUSState.login      = sdbus::createProxy(*m_sDBUSState.connection, sdbus::ServiceName{"org.freedesktop.login1"}, sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (sdbus::Error& e) {
        Debug::log(ERR, "fprint: Failed to setup dbus ({})", e.what());
        m_sDBUSState.connection.reset();
        return;
    }

    m_sDBUSState.login->getPropertyAsync("PreparingForSleep").onInterface(LOGIN_MANAGER).uponReplyInvoke([this](std::optional<sdbus::Error> e, sdbus::Variant preparingForSleep) {
        if (e) {
            Debug::log(WARN, "fprint: Failed getting value for PreparingForSleep: {}", e->what());
            return;
        }
        m_sDBUSState.sleeping = preparingForSleep.get<bool>();
        // When entering sleep, the wake signal will trigger startVerify().
        if (m_sDBUSState.sleeping)
            return;
        inhibitSleep();
        startVerify();
    });
    m_sDBUSState.login->uponSignal("PrepareForSleep").onInterface(LOGIN_MANAGER).call([this](bool start) {
        Debug::log(LOG, "fprint: PrepareForSleep (start: {})", start);
        if (start) {
            m_sDBUSState.sleeping = true;
            stopVerify();
            m_sDBUSState.inhibitLock.reset();
        } else {
            m_sDBUSState.sleeping = false;
            inhibitSleep();
            startVerify();
        }
    });
}

void CFingerprint::handleInput(const std::string& input) {
    ;
}

std::optional<std::string> CFingerprint::getLastFailText() {
    if (!m_sFailureReason.empty())
        return std::optional(m_sFailureReason);
    return std::nullopt;
}

std::optional<std::string> CFingerprint::getLastPrompt() {
    if (!m_sPrompt.empty())
        return std::optional(m_sPrompt);
    return std::nullopt;
}

bool CFingerprint::checkWaiting() {
    return false;
}

void CFingerprint::terminate() {
    if (!m_sDBUSState.abort)
        releaseDevice();
}

std::shared_ptr<sdbus::IConnection> CFingerprint::getConnection() {
    return m_sDBUSState.connection;
}

void CFingerprint::inhibitSleep() {
    m_sDBUSState.login->callMethodAsync("Inhibit")
        .onInterface(LOGIN_MANAGER)
        .withArguments("sleep", "hyprlock", "Fingerprint verifcation must be stopped before sleep", "delay")
        .uponReplyInvoke([this](std::optional<sdbus::Error> e, sdbus::UnixFd fd) {
            if (e)
                Debug::log(WARN, "fprint: could not inhibit sleep: {}", e->what());
            else
                m_sDBUSState.inhibitLock = fd;
        });
}

bool CFingerprint::createDeviceProxy() {
    auto              proxy = sdbus::createProxy(*m_sDBUSState.connection, FPRINT, sdbus::ObjectPath{"/net/reactivated/Fprint/Manager"});

    sdbus::ObjectPath path;
    try {
        proxy->callMethod("GetDefaultDevice").onInterface(MANAGER).storeResultsTo(path);
    } catch (sdbus::Error& e) {
        Debug::log(WARN, "fprint: couldn't connect to Fprint service ({})", e.what());
        return false;
    }
    Debug::log(LOG, "fprint: using device path {}", path.c_str());
    m_sDBUSState.device = sdbus::createProxy(*m_sDBUSState.connection, FPRINT, path);

    m_sDBUSState.device->uponSignal("VerifyFingerSelected").onInterface(DEVICE).call([](const std::string& finger) { Debug::log(LOG, "fprint: finger selected: {}", finger); });
    m_sDBUSState.device->uponSignal("VerifyStatus").onInterface(DEVICE).call([this](const std::string& result, const bool done) { handleVerifyStatus(result, done); });

    m_sDBUSState.device->uponSignal("PropertiesChanged")
        .onInterface("org.freedesktop.DBus.Properties")
        .call([this](const std::string& interface, const std::map<std::string, sdbus::Variant>& properties) {
            if (interface != DEVICE || m_sDBUSState.done)
                return;

            try {
                const auto presentVariant = properties.at("finger-present");
                bool       isPresent      = presentVariant.get<bool>();
                if (!isPresent)
                    return;
                m_sPrompt = m_sFingerprintPresent;
                g_pHyprlock->enqueueForceUpdateTimers();
            } catch (std::out_of_range& e) {}
        });

    return true;
}

void CFingerprint::handleVerifyStatus(const std::string& result, bool done) {
    Debug::log(LOG, "fprint: handling status {}", result);
    auto matchResult   = s_mapStringToTestType[result];
    bool authenticated = false;
    bool retry         = false;
    if (m_sDBUSState.sleeping && matchResult != MATCH_DISCONNECTED)
        return;
    switch (matchResult) {
        case MATCH_INVALID: Debug::log(WARN, "fprint: unknown status: {}", result); break;
        case MATCH_NO_MATCH:
            stopVerify();
            if (m_sDBUSState.retries >= 3) {
                m_sFailureReason = "Fingerprint auth disabled (too many failed attempts)";
            } else {
                done                         = false;
                static const auto RETRYDELAY = g_pConfigManager->getValue<Hyprlang::INT>("auth:fingerprint:retry_delay");
                g_pHyprlock->addTimer(std::chrono::milliseconds(*RETRYDELAY), [](std::shared_ptr<CTimer> self, void* data) { ((CFingerprint*)data)->startVerify(true); }, this);
                m_sFailureReason = "Fingerprint did not match";
            }
            break;
        case MATCH_UNKNOWN_ERROR:
            stopVerify();
            m_sFailureReason = "Fingerprint auth disabled (unknown error)";
            break;
        case MATCH_MATCHED:
            stopVerify();
            authenticated = true;
            g_pAuth->enqueueUnlock();
            break;
        case MATCH_RETRY:
            retry     = true;
            m_sPrompt = "Please retry fingerprint scan";
            break;
        case MATCH_SWIPE_TOO_SHORT:
            retry     = true;
            m_sPrompt = "Swipe too short - try again";
            break;
        case MATCH_FINGER_NOT_CENTERED:
            retry     = true;
            m_sPrompt = "Finger not centered - try again";
            break;
        case MATCH_REMOVE_AND_RETRY:
            retry     = true;
            m_sPrompt = "Remove your finger and try again";
            break;
        case MATCH_DISCONNECTED:
            m_sFailureReason   = "Fingerprint device disconnected";
            m_sDBUSState.abort = true;
            break;
    }

    if (!authenticated && !retry)
        g_pAuth->enqueueFail(m_sFailureReason, AUTH_IMPL_FINGERPRINT);

    if (done || m_sDBUSState.abort)
        m_sDBUSState.done = true;
}

void CFingerprint::claimDevice() {
    const auto currentUser = ""; // Empty string means use the caller's id.
    m_sDBUSState.device->callMethodAsync("Claim").onInterface(DEVICE).withArguments(currentUser).uponReplyInvoke([this](std::optional<sdbus::Error> e) {
        if (e)
            Debug::log(WARN, "fprint: could not claim device, {}", e->what());
        else {
            Debug::log(LOG, "fprint: claimed device");
            startVerify();
        }
    });
}

void CFingerprint::startVerify(bool isRetry) {
    if (!m_sDBUSState.device) {
        if (!createDeviceProxy())
            return;

        claimDevice();
        return;
    }
    auto finger = "any"; // Any finger.
    m_sDBUSState.device->callMethodAsync("VerifyStart").onInterface(DEVICE).withArguments(finger).uponReplyInvoke([this, isRetry](std::optional<sdbus::Error> e) {
        if (e) {
            Debug::log(WARN, "fprint: could not start verifying, {}", e->what());
            if (isRetry)
                m_sFailureReason = "Fingerprint auth disabled (failed to restart)";

        } else {
            Debug::log(LOG, "fprint: started verifying");
            if (isRetry) {
                m_sDBUSState.retries++;
                m_sPrompt = "Could not match fingerprint. Try again.";
            } else
                m_sPrompt = m_sFingerprintReady;
        }
        g_pHyprlock->enqueueForceUpdateTimers();
    });
}

bool CFingerprint::stopVerify() {
    if (!m_sDBUSState.device)
        return false;
    try {
        m_sDBUSState.device->callMethod("VerifyStop").onInterface(DEVICE);
    } catch (sdbus::Error& e) {
        Debug::log(WARN, "fprint: could not stop verifying, {}", e.what());
        return false;
    }
    Debug::log(LOG, "fprint: stopped verification");
    return true;
}

bool CFingerprint::releaseDevice() {
    if (!m_sDBUSState.device)
        return false;
    try {
        m_sDBUSState.device->callMethod("Release").onInterface(DEVICE);
    } catch (sdbus::Error& e) {
        Debug::log(WARN, "fprint: could not release device, {}", e.what());
        return false;
    }
    Debug::log(LOG, "fprint: released device");
    return true;
}
