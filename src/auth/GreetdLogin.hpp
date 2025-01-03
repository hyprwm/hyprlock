#pragma once

#include "Auth.hpp"
#include "../helpers/NotJson.hpp"
#include <condition_variable>
#include <string>
#include <expected>
#include <thread>

// GREETD PROTOCOL
enum eGreetdResponse : uint8_t {
    GREETD_RESPONSE_UNKNOWN = 0xff,
    GREETD_RESPONSE_SUCCESS = 0,
    GREETD_RESPONSE_ERROR   = 1,
    GREETD_RESPONSE_AUTH    = 2,
};

enum eGreetdErrorMessageType : uint8_t {
    GREETD_OK         = 0,
    GREETD_ERROR_AUTH = 1,
    GREETD_ERROR      = 2,
};

enum eGreetdAuthMessageType : uint8_t {
    GREETD_INITIAL      = 0,
    GREETD_AUTH_VISIBLE = 0,
    GREETD_AUTH_SECRET  = 1,
    GREETD_AUTH_INFO    = 2,
    GREETD_AUTH_ERROR   = 3,
};

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
    std::expected<NNotJson::SObject, eRequestError> request(const NNotJson::SObject& req);

    //
    void        createSession();
    void        cancelSession();
    void        recreateSession();
    void        startSessionAfterSuccess();

    void        handleResponse(const std::string& request, NNotJson::SObject& response);
    void        processInput();
    void        waitForInput();

    std::thread m_thread;

    int         m_socketFD      = -1;
    std::string m_loginUserName = "";

    bool        m_ok = true;
};
