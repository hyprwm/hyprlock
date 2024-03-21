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

        bool                    inputSubmitted = false;
        bool                    inputRequested = false;
        bool                    success        = false;
    };

    struct SFeedback {
        std::string text;
        bool        isFail = false;
    };

    void                     start();
    bool                     auth(std::string pam_module);
    bool                     didAuthSucceed();

    void                     waitForInput();
    void                     submitInput(const char* input);

    void                     setPrompt(const char* prompt);
    std::optional<SFeedback> getFeedback();

    bool                     checkWaiting();

    void                     terminate();

  private:
    SPamConversationState m_sConversationState;

    void                  resetConversation();
};

inline std::unique_ptr<CAuth> g_pAuth = std::make_unique<CAuth>();
