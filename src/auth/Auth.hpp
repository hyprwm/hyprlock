#pragma once

#include <optional>
#include <vector>

#include "../defines.hpp"

enum eAuthImplementations {
    AUTH_IMPL_PAM         = 0,
    AUTH_IMPL_FINGERPRINT = 1,
};

class IAuthImplementation {
  public:
    virtual ~IAuthImplementation() = default;

    virtual eAuthImplementations       getImplType()                         = 0;
    virtual void                       init()                                = 0;
    virtual void                       handleInput(const std::string& input) = 0;
    virtual bool                       checkWaiting()                        = 0;
    virtual std::optional<std::string> getLastFailText()                     = 0;
    virtual std::optional<std::string> getLastPrompt()                       = 0;
    virtual void                       terminate()                           = 0;

    friend class CAuth;
};

class CAuth {
  public:
    CAuth();

    void                       start();

    void                       submitInput(const std::string& input);
    bool                       checkWaiting();

    const std::string&         getCurrentFailText();

    std::optional<std::string> getFailText(eAuthImplementations implType);
    std::optional<std::string> getPrompt(eAuthImplementations implType);
    size_t                     getFailedAttempts();

    SP<IAuthImplementation>    getImpl(eAuthImplementations implType);

    void                       terminate();

    void                       enqueueUnlock();
    void                       enqueueFail(const std::string& failText, eAuthImplementations implType);

    // Should only be set via the main thread
    bool m_bDisplayFailText = false;

  private:
    struct {
        std::string          failText       = "";
        eAuthImplementations failSource     = AUTH_IMPL_PAM;
        size_t               failedAttempts = 0;
    } m_sCurrentFail;

    std::vector<SP<IAuthImplementation>> m_vImpls;
};

inline UP<CAuth> g_pAuth;
