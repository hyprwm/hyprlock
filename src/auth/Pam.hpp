#pragma once

#include "Auth.hpp"

#include <optional>
#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>

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

    ~CPam() override;
    eAuthImplementations getImplType() override {
        return AUTH_IMPL_PAM;
    }
    void                       init() override;
    void                       handleInput(const std::string& input) override;
    bool                       isAuthenticated() override;
    bool                       checkWaiting() override;
    std::optional<std::string> getLastFailText() override;
    std::optional<std::string> getLastPrompt() override;
    void                       terminate() override;

  private:
    SPamConversationState m_sConversationState;

    bool                  m_bBlockInput    = true;
    bool                  m_bAuthenticated = false;

    std::string           m_sPamModule;

    bool                  auth();
    void                  resetConversation();
};
