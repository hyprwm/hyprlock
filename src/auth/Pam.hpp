#pragma once

#include "Auth.hpp"

#include <optional>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>

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
        // Don't call pam_authenticate until the user submits something.
        // Used when another auth method can unlock in parallel, so that unlocking via that
        // method never aborts a live PAM conversation (which pam_unix counts as a failure).
        bool                  deferStart         = false;
        bool                  inputPreSubmitted  = false;
        std::string           preSubmittedPrompt = ""; // prompt that was shown when the pre-submitted input was collected
        std::string           firstPrompt        = ""; // first prompt of the most recent transaction
        bool                  firstPromptSeen    = false;

        std::atomic_bool      terminateRequested = false;
        std::function<void()> waitForInput       = []() {};
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
    virtual void                       terminate();

  private:
    std::thread           m_thread;
    SPamConversationState m_sConversationState;

    bool                  m_bBlockInput = false;

    std::string           m_sPamModule = "";
    std::string           m_username   = "";

    bool                  auth();
    void                  resetConversation();
};
