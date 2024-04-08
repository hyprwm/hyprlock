#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>

class CAuth {
  public:
    struct SPamConversationState {
        std::string             input      = "";
        std::string             prompt     = "";
        std::string             lastPrompt = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        bool                    waitingForPamAuth = false;
        bool                    inputRequested    = false;

        bool                    success = false;
    };

    struct SFeedback {
        std::string text;
        bool        isFail = false;
    };

    CAuth();

    void      start();
    bool      auth(std::string pam_module);
    bool      didAuthSucceed();

    void      waitForInput();
    void      submitInput(std::string input);

    void      setPrompt(const char* prompt);
    void      clearFailText();
    SFeedback getFeedback();

    bool      checkWaiting();

    void      terminate();

  private:
    SPamConversationState m_sConversationState;

    std::string           m_sFailText;
    bool                  m_bBlockInput = true;

    std::string           m_sPamModule;

    void                  resetConversation();
};

inline std::unique_ptr<CAuth> g_pAuth;
