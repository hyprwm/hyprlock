#pragma once

#include "Auth.hpp"

#include <optional>
#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <security/pam_appl.h>

class CPam : public IAuthImplementation {
  public:
    struct SPamConversationState {
        std::string             input    = "";
        std::string             prompt   = "";
        std::string             failText = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        bool                    waitingForPamAuth = false;
        bool                    inputRequested    = false;
        bool                    failTextFromPam   = false;
        std::function<void()>   waitForInput      = []() {};
    };

    CPam();

    void waitForInput();

    virtual ~CPam();
    virtual eAuthImplementations getImplType() {
        return AUTH_IMPL_PAM;
    }
    virtual void                       init();
    virtual void                       handleInput(const std::string& input);
    virtual bool                       checkWaiting();
    virtual std::optional<std::string> getLastFailText();
    virtual std::optional<std::string> getLastPrompt();
    virtual void                       clearHandle();
    virtual void                       terminate();

  private:
    std::thread           m_thread;
    SPamConversationState m_sConversationState;

    bool                  m_bBlockInput = true;

    std::string           m_sPamModule;
    int                   m_iRet = -1;
    pam_handle_t*         m_pHandle = nullptr;

    bool                  auth();
    void                  resetConversation();
};
