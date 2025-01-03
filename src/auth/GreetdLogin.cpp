#include "GreetdLogin.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/NotJson.hpp"
#include "../helpers/Log.hpp"

#include <hyprutils/string/VarList.hpp>
#include <hyprutils/os/Process.hpp>
#include <sys/socket.h>
#include <sys/un.h>

static constexpr eGreetdAuthMessageType messageTypeFromString(const std::string_view& type) {
    if (type == "visible")
        return GREETD_AUTH_VISIBLE;
    if (type == "secret")
        return GREETD_AUTH_SECRET;
    if (type == "info")
        return GREETD_AUTH_INFO;
    if (type == "error")
        return GREETD_AUTH_ERROR;
    return GREETD_AUTH_ERROR;
}

static constexpr eGreetdErrorMessageType errorTypeFromString(const std::string_view& type) {
    if (type == "auth_error")
        return GREETD_ERROR_AUTH;
    if (type == "error")
        return GREETD_ERROR;
    return GREETD_ERROR;
}

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

static bool sendGreetdRequest(int fd, const NNotJson::SObject& request) {
    if (fd < 0) {
        Debug::log(ERR, "[GreetdLogin] Invalid socket fd");
        return false;
    }

    const auto PAYLOAD = NNotJson::serialize(request);

    if (!request.values.contains("response"))
        Debug::log(TRACE, "[GreetdLogin] Request: {}", PAYLOAD);
    else
        Debug::log(TRACE, "[GreetdLogin] Request: REDACTED");

    if (sendToSock(fd, PAYLOAD) < 0) {
        Debug::log(ERR, "[GreetdLogin] Failed to send payload to greetd");
        return false;
    }

    return true;
}

void CGreetdLogin::init() {
    const auto LOGINUSER = g_pConfigManager->getValue<Hyprlang::STRING>("login:user");
    m_loginUserName      = *LOGINUSER;

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

std::expected<NNotJson::SObject, eRequestError> CGreetdLogin::request(const NNotJson::SObject& req) {
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

    const auto [RESULTOBJ, ERROR] = NNotJson::parse(RESPONSESTR);
    if (ERROR.status != NNotJson::SError::NOT_JSON_OK) {
        Debug::log(ERR, "[GreetdLogin] Failed to parse response from greetd: {}", ERROR.message);
        m_ok = false;
        return std::unexpected(GREETD_REQUEST_ERROR_PARSE);
    }

    if (!RESULTOBJ.values.contains("type")) {
        Debug::log(ERR, "[GreetdLogin] Invalid greetd response");
        m_ok = false;
        return std::unexpected(GREETD_REQUEST_ERROR_PARSE);
    }

    return RESULTOBJ;
}

inline static const std::string& getStringValue(NNotJson::SObject& obj, const std::string& key) {
    try {
        return std::get<std::string>(obj.values[key]);
    } catch (std::bad_variant_access const& ex) { RASSERT(false, "Key \"{}\" does not contain a string", key); }
};

void CGreetdLogin::handleResponse(const std::string& request, NNotJson::SObject& response) {
    const auto RESPONSETYPE = getStringValue(response, "type");

    if (RESPONSETYPE == "error") {
        const auto ERRORTYPE = getStringValue(response, "error_type");
        m_state.errorType    = errorTypeFromString(ERRORTYPE);
        m_state.error        = getStringValue(response, "description");
        Debug::log(ERR, "[GreetdLogin] Request failed: {} - {}", ERRORTYPE, m_state.error);
        // Don't post a fail if this is a response to "cancel_session"
        if (!m_state.error.empty() && request != "cancel_session")
            g_pAuth->enqueueFail(m_state.error, AUTH_IMPL_GREETD);

        // We don't have to cancel if "create_session" failed
        if (request != "create_session")
            cancelSession();
    } else if (RESPONSETYPE == "auth_message") {
        const auto AUTHMESSAGETYPE = getStringValue(response, "auth_message_type");
        m_state.authMessageType    = messageTypeFromString(AUTHMESSAGETYPE);
        m_state.message            = getStringValue(response, "auth_message");
        Debug::log(LOG, "[GreetdLogin] Auth message: {} - {}", AUTHMESSAGETYPE, m_state.message);
        if (m_state.authMessageType == GREETD_AUTH_ERROR && !m_state.message.empty())
            g_pAuth->enqueueFail(m_state.message, AUTH_IMPL_GREETD);

    } else if (RESPONSETYPE == "success") {
        if (request == "create_session" || request == "post_auth_message_response")
            startSessionAfterSuccess();
    } else
        Debug::log(ERR, "Unknown response type \"{}\"", RESPONSETYPE);
}

void CGreetdLogin::startSessionAfterSuccess() {
    const auto                  SELECTEDSESSION = g_pHyprlock->getSelectedGreetdLoginSession();
    Hyprutils::String::CVarList args(SELECTEDSESSION.exec, 0, ' ');

    NNotJson::SObject           startSession{.values = {
                                       {"type", "start_session"},
                                   }};
    startSession.values["cmd"] = std::vector<std::string>{args.begin(), args.end()};

    // TODO: Is there a response for this? Should we check it?
    if (!sendGreetdRequest(m_socketFD, startSession))
        m_ok = false;
    else {
        if (g_pHyprlock->m_sCurrentDesktop == "Hyprland")
            g_pHyprlock->spawnSync("hyprctl dispatch exit");
        else
            g_pAuth->enqueueUnlock();
    }
}

void CGreetdLogin::cancelSession() {
    NNotJson::SObject cancelSession{
        .values =
            {
                {"type", "cancel_session"},
            },
    };

    auto RESPONSEOPT = request(cancelSession);
    if (!RESPONSEOPT.has_value())
        return;

    handleResponse("cancel_session", RESPONSEOPT.value());

    m_state.authMessageType = GREETD_INITIAL;
    m_state.errorType       = GREETD_OK;
}

void CGreetdLogin::createSession() {
    if (m_state.authMessageType != GREETD_INITIAL && m_state.errorType != GREETD_ERROR_AUTH)
        Debug::log(WARN, "[GreetdLogin] Trying to create a session, but last one still active?");

    NNotJson::SObject createSession = {
        .values =
            {
                {"type", "create_session"},
                {"username", m_loginUserName},
            },
    };

    Debug::log(INFO, "Creating session for user {}", m_loginUserName);

    auto RESPONSEOPT = request(createSession);
    if (!RESPONSEOPT.has_value()) {
        Debug::log(ERR, "Failed to create session: {}", getErrorString(RESPONSEOPT.error()));
        return;
    }

    handleResponse("create_session", RESPONSEOPT.value());
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
        NNotJson::SObject postAuthMessageResponse{
            .values =
                {
                    {"type", "post_auth_message_response"},
                    {"response", ""},
                },
        };
        auto RESPONSEOPT = request(postAuthMessageResponse);
        if (!RESPONSEOPT.has_value()) {
            Debug::log(ERR, "Failed to create session: {}", getErrorString(RESPONSEOPT.error()));
            return;
        }

        handleResponse("post_auth_message_response", RESPONSEOPT.value());
    }

    if (m_state.errorType != GREETD_OK) {
        Debug::log(LOG, "Empty response to a info message failed!");
        return;
    }

    NNotJson::SObject postAuthMessageResponse{
        .values =
            {
                {"type", "post_auth_message_response"},
                {"response", m_state.input},
            },
    };

    auto RESPONSEOPT = request(postAuthMessageResponse);
    if (!RESPONSEOPT.has_value()) {
        Debug::log(ERR, "Failed to send auth response: {}", getErrorString(RESPONSEOPT.error()));
        return;
    }

    handleResponse("post_auth_message_response", RESPONSEOPT.value());
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
    if (m_socketFD > 0)
        close(m_socketFD);

    m_socketFD = -1;
}
