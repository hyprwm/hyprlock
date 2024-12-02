#include "PwAuth.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include <cstddef>
#include <cstring>
#define GCRYPT_NO_DEPRECATED
#define GCRYPT_NO_MPI_MACROS
#define NEED_LIBGCRYPT_VERSION nullptr
#include <gcrypt.h>

using namespace std::chrono_literals;

static std::unique_ptr<unsigned char[]> hex2Bytes(const std::string& hex) noexcept {
    auto bytes = std::make_unique<unsigned char[]>(hex.length() / 2);
    for (std::size_t i = 0; i < hex.length() / 2; ++i) {
        try {
            auto v = std::stoi(hex.substr(2 * i, 2), nullptr, 16);
            if (v >= 0)
                bytes[i] = static_cast<unsigned char>(v);
            else
                throw std::invalid_argument("invalid hex value");
        } catch (std::invalid_argument const& e) {
            Debug::log(ERR, "auth: invalid password_hash");
            bytes = nullptr;
        } catch (std::out_of_range const& e) {
            // Should never happen, as 2-byte substrings should never go o-o-r.
            Debug::log(CRIT, "auth: implementation error in hex2Bytes conversion");
            bytes = nullptr;
        }
    }
    return bytes;
}

static std::string bytes2Hex(const unsigned char* bytes, std::size_t len) {
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << std::hex;
    for (std::size_t i = 0; i < len; ++i)
         ss << (int)bytes[i];
    return ss.str();
}

CPwAuth::CPwAuth() {

    if (gcry_check_version(NEED_LIBGCRYPT_VERSION))
        gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
    else
        Debug::log(CRIT, "libgcrypt too old");

    if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {

        // Handle the hash algorithm
        static auto const ALGO = *(Hyprlang::STRING*)(g_pConfigManager->getValuePtr("general:hash_algorithm"));
        m_iAlgo                 = gcry_md_map_name(ALGO);
        m_iDigestLen            = gcry_md_get_algo_dlen(m_iAlgo);
        if (m_iAlgo) {
            static auto const err = gcry_err_code(gcry_md_test_algo(m_iAlgo));
            if (err == GPG_ERR_NO_ERROR) {

                // Handle the salt
                static auto* const SALT = (Hyprlang::STRING*)(g_pConfigManager->getValuePtr("general:hash_salt"));
                m_szSalt                = std::string(*SALT);

                // Handle the expected hash
                static auto* const HASH = (Hyprlang::STRING*)(g_pConfigManager->getValuePtr("general:password_hash"));
                static auto const  hash = std::string(*HASH);
                if (hash.empty() || (hash.size() % 2) || (hash.length() != 2uL * m_iDigestLen)) {
                    Debug::log(ERR, "auth: password_hash has incorrect length for algorithm {} (got: {}, expected: {})", ALGO, hash.size(), 2uL * m_iDigestLen);
                    m_bLibFailed = true;
                } else {
                    m_aHash = hex2Bytes(hash);
                    if (!m_aHash || hash.empty())
                        m_bLibFailed = true;
                }
            } else {
                // Might be due to FIPS mode
                Debug::log(CRIT, "auth: hash algorithm unavailable: {}", ALGO);
                m_bLibFailed = true;
            }
        } else {
            Debug::log(ERR, "auth: unknown hash algorithm: {}", ALGO);
            m_bLibFailed = true;
        }
    } else {
        Debug::log(CRIT, "libgcrypt could not be initialized");
        m_bLibFailed = true;
    }
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->onPasswordCheckTimer();
}

void CPwAuth::start() {
    std::thread([this]() {
        reset();

        waitForInput();

        // For grace or SIGUSR1 unlocks
        if (g_pHyprlock->isUnlocked())
            return;

        const auto AUTHENTICATED = auth();
        m_bAuthenticated         = AUTHENTICATED;

        if (g_pHyprlock->isUnlocked())
            return;

        g_pHyprlock->addTimer(1ms, passwordCheckTimerCallback, nullptr);
    }).detach();
}

bool CPwAuth::auth() {
    if (m_bLibFailed)
        return true;

    bool verdict;
    auto digest  = std::make_unique<unsigned char[]>(m_iDigestLen);
    auto istr    = m_sState.input;
    istr.append(m_szSalt);

    gcry_md_hash_buffer(m_iAlgo, digest.get(), istr.c_str(), istr.size());
    Debug::log(TRACE, "auth: resulting hash {}", bytes2Hex(digest.get(), m_iDigestLen));
    Debug::log(TRACE, "auth:  expected hash {}", bytes2Hex(m_aHash.get(), m_iDigestLen));
    verdict = !std::memcmp(m_aHash.get(), digest.get(), m_iDigestLen);

    if (verdict)
        Debug::log(LOG, "auth: authenticated");
    else
        Debug::log(ERR, "auth: unsuccessful");

    m_sState.authenticating = false;
    /// DEBUG Code; replace constant with verdict
    return verdict;
}

bool CPwAuth::isAuthenticated() {
    return m_bAuthenticated;
}

// clearing the input must be done from the main thread
static void clearInputTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->clearPasswordBuffer();
}

void CPwAuth::waitForInput() {
    g_pHyprlock->addTimer(1ms, clearInputTimerCallback, nullptr);
    if (m_bLibFailed)
        return;

    std::unique_lock<std::mutex> lk(m_sState.inputMutex);
    m_bBlockInput           = false;
    m_sState.inputRequested = true;
    m_sState.inputSubmittedCondition.wait(lk, [this] { return !m_sState.inputRequested || g_pHyprlock->m_bTerminate; });
    m_bBlockInput = true;
}

void CPwAuth::submitInput(std::string input) {
    std::unique_lock<std::mutex> lk(m_sState.inputMutex);
    if (!m_sState.inputRequested)
        Debug::log(ERR, "SubmitInput called, but the auth thread is not waiting for input!");
    m_sState.input          = input;
    m_sState.inputRequested = false;
    m_sState.authenticating = true;
    m_sState.inputSubmittedCondition.notify_all();
}

std::optional<std::string> CPwAuth::getLastPrompt() {
    std::string pmpt = "Password: ";
    return pmpt;
}

std::optional<std::string> CPwAuth::getLastFailText() {
    std::string ret = "Password incorrect";
    return ret;
}

bool CPwAuth::checkWaiting() {
    return m_bBlockInput;
}

void CPwAuth::terminate() {
    m_sState.inputSubmittedCondition.notify_all();
}

void CPwAuth::reset() {
    m_sState.input          = "";
    m_sState.inputRequested = false;
    m_sState.authenticating = false;
}
