#pragma once

#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include <condition_variable>

class CAuth {
  public:
    struct SPamConversationState {
        std::string             input    = "";
        std::string             prompt   = "";
        std::string             failText = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        bool                    waitingForPamAuth = false;
        bool                    inputRequested    = false;

        bool                    success = false;
    };

    CAuth();

    void                       start();
    bool                       auth();
    bool                       didAuthSucceed();

    void                       waitForInput();
    void                       submitInput(std::string input);

    std::optional<std::string> getLastFailText();
    std::optional<std::string> getLastPrompt();

    bool                       checkWaiting();

    void                       terminate();

    // Should only be set via the main thread
    bool m_bDisplayFailText = false;

  private:
    SPamConversationState m_sConversationState;

    bool                  m_bBlockInput = true;

    std::string           m_sPamModule;

    void                  resetConversation();
};

inline std::unique_ptr<CAuth> g_pAuth;
