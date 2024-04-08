#pragma once

#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include <condition_variable>

class CAuth {
  public:
    struct SPamConversationState {
        std::string             input      = "";
        std::string             prompt     = "";
        std::string             lastPrompt = "";
        std::string             failReason = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        bool                    waitingForPamAuth = false;
        bool                    inputRequested    = false;

        bool                    blockInput     = false;
        std::string             unhandledInput = "";

        bool                    success = false;
    };

    struct SFeedback {
        std::string text;
        bool        isFail = false;
    };

    CAuth();

    void                     start();
    bool                     auth(std::string pam_module);
    bool                     didAuthSucceed();

    void                     waitForInput();
    void                     submitInput(std::optional<std::string> input);

    void                     setPrompt(const char* prompt);
    std::optional<SFeedback> getFeedback();

    bool                     checkWaiting();

    void                     terminate();

  private:
    SPamConversationState m_sConversationState;

    std::string           m_sPamModule;

    void                  resetConversation();
};

inline std::unique_ptr<CAuth> g_pAuth;
