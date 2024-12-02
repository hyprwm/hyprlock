#pragma once

#include "IAuth.hpp"
#include <optional>
#include <string>
#include <mutex>
#include <condition_variable>

class CAuth : public CIAuth {
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
    };

    explicit CAuth();
    CAuth(const CAuth&) = delete;

    void                       start();
    bool                       auth();
    bool                       isAuthenticated();

    void                       waitForInput();
    void                       submitInput(std::string input);

    std::optional<std::string> getLastFailText();
    std::optional<std::string> getLastPrompt();

    bool                       checkWaiting();

    void                       terminate();

  private:
    SPamConversationState m_sConversationState;

    bool                  m_bBlockInput    = true;
    bool                  m_bAuthenticated = false;

    std::string           m_sPamModule;

    void                  resetConversation();
};
