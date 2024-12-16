#pragma once

#include "IAuth.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

class CPwAuth : public CIAuth {
  public:
    struct SState {
        std::string             input = "";

        std::mutex              inputMutex;
        std::condition_variable inputSubmittedCondition;

        bool                    inputRequested = false;
        bool                    authenticating = false;
    };

    explicit CPwAuth();
    CPwAuth(const CPwAuth&) = delete;

    void                       start();
    bool                       auth();
    bool                       isAuthenticated();
    void                       waitForInput();
    void                       submitInput(std::string input);

    std::optional<std::string> getLastPrompt();
    std::optional<std::string> getLastFailText();

    bool                       checkWaiting();
    void                       terminate();

  private:
    SState                           m_sState;
    bool                             m_bBlockInput    = true;
    bool                             m_bAuthenticated = false;
    bool                             m_bLibFailed     = false;
    std::unique_ptr<unsigned char[]> m_aHash;
    std::string                      m_szSalt;
    int                              m_iAlgo = -1;
    unsigned int                     m_iDigestLen = 0;

    void                             reset();
};
