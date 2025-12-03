#pragma once

#include "Auth.hpp"

#include <condition_variable>
#include <optional>
#include <string>
#include <hyprlang.hpp>
#include <thread>

class CSodiumPWHash : public IAuthImplementation {
  public:
    enum EState {
        SODIUMHASH_IDLE  = 0,
        SODIUMHASH_INPUT = 1,
        SODIUMHASH_AUTH  = 2,
    };
    struct SCheckerState {
        std::string             input    = "";
        std::string             prompt   = "";
        std::string             failText = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        EState                  state = SODIUMHASH_IDLE;
    };

    CSodiumPWHash();

    virtual ~CSodiumPWHash();
    virtual eAuthImplementations getImplType() {
        return AUTH_IMPL_SODIUMPWHASH;
    }
    virtual void                       init();
    virtual void                       handleInput(const std::string& input);
    virtual bool                       checkWaiting();
    virtual std::optional<std::string> getLastFailText();
    virtual std::optional<std::string> getLastPrompt();
    virtual void                       terminate();

  private:
    bool              m_bBlockInput;
    Hyprlang::CConfig m_config;
    std::thread       m_thread;
    SCheckerState     m_sCheckerState;

    bool              auth();
    void* const*      getConfigValuePtr(const std::string& name);
    void              waitForInput();
};
