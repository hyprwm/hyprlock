#include "GreetdLogin.hpp"
#include "../config/ConfigManager.hpp"
#include "../config/LoginSessionManager.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <hyprutils/string/VarList.hpp>
#include <hyprutils/os/Process.hpp>
#include <sys/socket.h>
#include <sys/un.h>

static constexpr std::string getErrorString(eRequestError error) {
    switch (error) {
        case GREETD_REQUEST_ERROR_SEND: return "Failed to send payload to greetd";
        case GREETD_REQUEST_ERROR_READ: return "Failed to read response from greetd";
        case GREETD_REQUEST_ERROR_PARSE: return "Failed to parse response from greetd";
        case GREETD_REQUEST_ERROR_FORMAT: return "Invalid greetd response";
        default: return "Unknown error";
    }
};

static int socketConnect() {
    const int FD = socket(AF_UNIX, SOCK_STREAM, 0);
    if (FD < 0) {
        Debug::log(ERR, "Failed to create socket");
        return -1;
    }

    sockaddr_un serverAddress = {.sun_family = 0};
    serverAddress.sun_family  = AF_UNIX;

    const auto PGREETDSOCK = std::getenv("GREETD_SOCK");

    if (PGREETDSOCK == nullptr) {
        Debug::log(ERR, "GREETD_SOCK not set!");
        return -1;
    }

    strncpy(serverAddress.sun_path, PGREETDSOCK, sizeof(serverAddress.sun_path) - 1);

    if (connect(FD, (sockaddr*)&serverAddress, SUN_LEN(&serverAddress)) < 0) {
        Debug::log(ERR, "Failed to connect to greetd socket");
        return -1;
    }

    return FD;
}

// send <payload-length> <payload>
static int sendToSock(int fd, const std::string& PAYLOAD) {
    const uint32_t LEN   = PAYLOAD.size();
    uint32_t       wrote = 0;

    while (wrote < sizeof(LEN)) {
        auto n = write(fd, (char*)&LEN + wrote, sizeof(LEN) - wrote);
        if (n < 1) {
            Debug::log(ERR, "Failed to write to greetd socket");
            return -1;
        }

        wrote += n;
    }

    wrote = 0;

    while (wrote < LEN) {
        auto n = write(fd, PAYLOAD.c_str() + wrote, LEN - wrote);
        if (n < 1) {
            Debug::log(ERR, "Failed to write to greetd socket");
            return -1;
        }

        wrote += n;
    }

    return 0;
}

// read <payload-length> <payload>
static std::string readFromSock(int fd) {
    uint32_t len     = 0;
    uint32_t numRead = 0;

    while (numRead < sizeof(len)) {
        auto n = read(fd, (char*)&len + numRead, sizeof(len) - numRead);
        if (n < 1) {
            Debug::log(ERR, "Failed to read from greetd socket");
            return "";
        }

        numRead += n;
    }

    numRead = 0;
    std::string msg(len, '\0');

    while (numRead < len) {
        auto n = read(fd, msg.data() + numRead, len - numRead);
        if (n < 1) {
            Debug::log(ERR, "Failed to read from greetd socket");
            return "";
        }

        numRead += n;
    }

    return msg;
}

static bool sendGreetdRequest(int fd, const VGreetdRequest& request) {
    if (fd < 0) {
        Debug::log(ERR, "[GreetdLogin] Invalid socket fd");
        return false;
    }

    const auto GLZRESULT = glz::write_json(request);

    if (!GLZRESULT.has_value()) {
        const auto GLZERRORSTR = glz::format_error(GLZRESULT.error());
        Debug::log(ERR, "[GreetdLogin] Failed to serialize request: {}", GLZERRORSTR);
        return false;
    }

    if (std::holds_alternative<SGreetdPostAuthMessageResponse>(request))
        Debug::log(TRACE, "[GreetdLogin] Request: REDACTED");
    else
        Debug::log(TRACE, "[GreetdLogin] Request: {}", GLZRESULT.value());

    if (sendToSock(fd, GLZRESULT.value()) < 0) {
        Debug::log(ERR, "[GreetdLogin] Failed to send payload to greetd");
        return false;
    }

    return true;
}

void CGreetdLogin::init() {
    const auto LOGINUSER = g_pConfigManager->getValue<Hyprlang::STRING>("login:user");
    m_loginUserName      = *LOGINUSER;

    if (m_loginUserName.empty()) {
        Debug::log(ERR, "[GreetdLogin] No user specified");
        m_ok = false;
        return;
    } else
        Debug::log(LOG, "[GreetdLogin] Login user: {}", m_loginUserName);

    m_thread = std::thread([this]() {
        m_socketFD = socketConnect();
        if (m_socketFD < 0) {
            Debug::log(ERR, "[GreetdLogin] Failed to connect to greetd socket");
            m_ok = false;
        }

        while (true) {
            waitForInput();
            processInput();
            m_state.inputSubmitted = false;
            m_state.input.clear();
        }
    });
}

std::expected<VGreetdResponse, eRequestError> CGreetdLogin::request(const VGreetdRequest& req) {
    if (!sendGreetdRequest(m_socketFD, req)) {
        m_ok = false;
        return std::unexpected(GREETD_REQUEST_ERROR_SEND);
    }

    const auto RESPONSESTR = readFromSock(m_socketFD);
    if (RESPONSESTR.empty()) {
        Debug::log(ERR, "[GreetdLogin] Failed to read response from greetd");
        m_ok = false;
        return std::unexpected(GREETD_REQUEST_ERROR_READ);
    }

    Debug::log(TRACE, "[GreetdLogin] Response: {}", RESPONSESTR);

    const auto GLZRESULT = glz::read_json<VGreetdResponse>(RESPONSESTR);
    if (!GLZRESULT.has_value()) {
        const auto GLZERRORSTR = glz::format_error(GLZRESULT.error(), RESPONSESTR);
        Debug::log(ERR, "[GreetdLogin] Failed to parse response from greetd: {}", GLZERRORSTR);
        m_ok = false;
        return std::unexpected(GREETD_REQUEST_ERROR_PARSE);
    }

    return GLZRESULT.value();
}

void CGreetdLogin::handleResponse(const VGreetdRequest& request, const VGreetdResponse& response) {
    if (std::holds_alternative<SGreetdErrorResponse>(response)) {
        const auto ERRORRESPONSE = std::get<SGreetdErrorResponse>(response);
        m_state.errorType        = ERRORRESPONSE.error_type;
        m_state.error            = ERRORRESPONSE.description;
        Debug::log(ERR, "[GreetdLogin] Request failed: {} - {}", (int)m_state.errorType, m_state.error);
        // Don't post a fail if this is a response to "cancel_session"
        if (!m_state.error.empty() && !std::holds_alternative<SGreetdCancelSession>(request))
            g_pAuth->enqueueFail(m_state.error, AUTH_IMPL_GREETD);

        // We don't have to cancel if "create_session" failed
        if (!std::holds_alternative<SGreetdCreateSession>(request))
            cancelSession();
    } else if (std::holds_alternative<SGreetdAuthMessageResponse>(response)) {
        const auto AUTHMESSAGERESPONSE = std::get<SGreetdAuthMessageResponse>(response);
        m_state.authMessageType        = AUTHMESSAGERESPONSE.auth_message_type;
        m_state.message                = AUTHMESSAGERESPONSE.auth_message;
        Debug::log(LOG, "[GreetdLogin] Auth message: {} - {}", (int)m_state.authMessageType, m_state.message);
        if (m_state.authMessageType == GREETD_AUTH_ERROR && !m_state.message.empty())
            g_pAuth->enqueueFail(m_state.message, AUTH_IMPL_GREETD);

    } else if (std::holds_alternative<SGreetdSuccessResponse>(response)) {
        if (std::holds_alternative<SGreetdCreateSession>(request) || std::holds_alternative<SGreetdPostAuthMessageResponse>(request))
            startSessionAfterSuccess();
    } else
        Debug::log(ERR, "Unknown response from greetd");
}

void CGreetdLogin::startSessionAfterSuccess() {
    const auto                  SELECTEDSESSION = g_pLoginSessionManager->getSelectedLoginSession();
    Hyprutils::String::CVarList args(SELECTEDSESSION.exec, 0, ' ');

    SGreetdStartSession         startSession;
    startSession.cmd = std::vector<std::string>{args.begin(), args.end()};

    const auto REQUEST = VGreetdRequest{startSession};
    // TODO: Is there a response for this? Should we check it?
    if (!sendGreetdRequest(m_socketFD, REQUEST))
        m_ok = false;
    else {
        if (g_pHyprlock->m_currentDesktop == "Hyprland")
            spawnSync("hyprctl dispatch exit");
        else
            g_pAuth->enqueueUnlock();
    }
}

void CGreetdLogin::cancelSession() {
    SGreetdCancelSession cancelSession;

    const auto           REQUEST  = VGreetdRequest{cancelSession};
    const auto           RESPONSE = request(REQUEST);

    if (!RESPONSE.has_value()) {
        Debug::log(ERR, "Failed to cancel session: {}", getErrorString(RESPONSE.error()));
        return;
    }

    handleResponse(REQUEST, RESPONSE.value());

    m_state.authMessageType = GREETD_INITIAL;
    m_state.errorType       = GREETD_OK;
}

void CGreetdLogin::createSession() {
    if (m_state.authMessageType != GREETD_INITIAL && m_state.errorType != GREETD_ERROR_AUTH)
        Debug::log(WARN, "[GreetdLogin] Trying to create a session, but last one still active?");

    Debug::log(LOG, "Creating session for user {}", m_loginUserName);

    SGreetdCreateSession createSession;
    createSession.username = m_loginUserName;

    const auto REQUEST  = VGreetdRequest{createSession};
    const auto RESPONSE = request(REQUEST);

    if (!RESPONSE.has_value()) {
        Debug::log(ERR, "Failed to create session: {}", getErrorString(RESPONSE.error()));
        return;
    }

    handleResponse(REQUEST, RESPONSE.value());
}

void CGreetdLogin::processInput() {
    if (!m_ok) {
        g_pAuth->enqueueFail("Greetd login NOK", AUTH_IMPL_GREETD);
        return;
    }

    if (m_state.authMessageType == GREETD_INITIAL)
        createSession();

    while (m_ok && (m_state.authMessageType == GREETD_AUTH_INFO || m_state.authMessageType == GREETD_AUTH_ERROR)) {
        // Empty reply
        SGreetdPostAuthMessageResponse postAuthMessageResponse;

        const auto                     REQUEST  = VGreetdRequest{postAuthMessageResponse};
        const auto                     RESPONSE = request(REQUEST);

        if (!RESPONSE.has_value()) {
            Debug::log(ERR, "Failed to create session: {}", getErrorString(RESPONSE.error()));
            return;
        }

        handleResponse(REQUEST, RESPONSE.value());
    }

    if (m_state.errorType != GREETD_OK) {
        // TODO: this error message is not good
        Debug::log(LOG, "Empty response to a info message failed!");
        return;
    }

    SGreetdPostAuthMessageResponse postAuthMessageResponse;
    postAuthMessageResponse.response = m_state.input;

    const auto REQUEST  = VGreetdRequest{postAuthMessageResponse};
    const auto RESPONSE = request(REQUEST);

    if (!RESPONSE.has_value()) {
        Debug::log(ERR, "Failed to send auth response: {}", getErrorString(RESPONSE.error()));
        return;
    }

    handleResponse(REQUEST, RESPONSE.value());
};

void CGreetdLogin::waitForInput() {
    std::unique_lock<std::mutex> lk(m_state.inputMutex);
    m_state.inputSubmittedCondition.wait(lk, [this] { return m_state.inputSubmitted; });
}

void CGreetdLogin::handleInput(const std::string& input) {
    std::unique_lock<std::mutex> lk(m_state.inputMutex);

    m_state.input          = input;
    m_state.inputSubmitted = true;

    m_state.inputSubmittedCondition.notify_all();
}

bool CGreetdLogin::checkWaiting() {
    return m_state.inputSubmitted;
}

std::optional<std::string> CGreetdLogin::getLastFailText() {
    if (!m_state.error.empty()) {
        return m_state.error;
    } else if (m_state.authMessageType == GREETD_AUTH_ERROR)
        return m_state.message;

    return std::nullopt;
}

std::optional<std::string> CGreetdLogin::getLastPrompt() {
    if (!m_state.message.empty())
        return m_state.message;
    return std::nullopt;
}

void CGreetdLogin::terminate() {
    m_state.inputSubmittedCondition.notify_all();
    if (m_thread.joinable())
        m_thread.join();

    if (m_socketFD > 0)
        close(m_socketFD);

    m_socketFD = -1;
}
