#pragma once

#include "Auth.hpp"
#include "GreetdProto.hpp"
#include "../config/LoginSessionManager.hpp"
#include <condition_variable>
#include <string>
#include <expected>
#include <thread>

// INTERNAL
enum eRequestError : uint8_t {
    GREETD_REQUEST_ERROR_SEND   = 0,
    GREETD_REQUEST_ERROR_READ   = 1,
    GREETD_REQUEST_ERROR_PARSE  = 2,
    GREETD_REQUEST_ERROR_FORMAT = 3,
};

class CGreetdLogin : public IAuthImplementation {
  public:
    virtual ~CGreetdLogin() = default;

    virtual eAuthImplementations getImplType() {
        return AUTH_IMPL_GREETD;
    }
    virtual void                       init();
    virtual void                       handleInput(const std::string& input);
    virtual bool                       checkWaiting();
    virtual std::optional<std::string> getLastFailText();
    virtual std::optional<std::string> getLastPrompt();
    virtual void                       terminate();

    struct {
        std::string             error           = "";
        eGreetdErrorMessageType errorType       = GREETD_OK;
        std::string             message         = "";
        eGreetdAuthMessageType  authMessageType = GREETD_INITIAL;

        std::mutex              inputMutex;
        std::string             input;
        bool                    inputSubmitted = false;
        std::condition_variable inputSubmittedCondition;
    } m_state;

    friend class CAuth;

  private:
    std::expected<VGreetdResponse, eRequestError> request(const VGreetdRequest& req);

    //
    void        createSession();
    void        cancelSession();
    void        startSessionAfterSuccess();

    void        handleResponse(const VGreetdRequest& request, const VGreetdResponse& response);
    void        processInput();
    void        waitForInput();

    std::thread m_thread;

    int         m_socketFD      = -1;
    std::string m_loginUserName = "";

    bool        m_ok = true;
};
