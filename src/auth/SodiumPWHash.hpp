#pragma once

#include "Auth.hpp"

#include <condition_variable>
#include <optional>
#include <string>
#include <hyprlang.hpp>
#include <thread>

class CSodiumPWHash : public IAuthImplementation {
  public:
    CSodiumPWHash();

    virtual ~CSodiumPWHash();
    virtual eAuthImplementations getImplType() {
        return AUTH_IMPL_SODIUM;
    }
    virtual void                       init();
    virtual void                       handleInput(const std::string& input);
    virtual bool                       checkWaiting();
    virtual std::optional<std::string> getLastFailText();
    virtual std::optional<std::string> getLastPrompt();
    virtual void                       terminate();

  private:
    void* const* getConfigValuePtr(const std::string& name);

    struct {
        std::condition_variable requestCV;
        std::string             input;
        bool                    requested = false;
        std::mutex              requestMutex;
        std::string             failText;
    } m_sCheckerState;

    std::thread       m_checkerThread;
    void              checkerLoop();

    void              rehash(std::string& input);

    Hyprlang::CConfig m_config;
};
