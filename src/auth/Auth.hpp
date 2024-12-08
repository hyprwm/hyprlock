#pragma once

#include <memory>
#include <optional>
#include <vector>

enum eAuthImplementations {
    AUTH_IMPL_PAM         = -1,
    AUTH_IMPL_FINGERPRINT = 1,
};

class IAuthImplementation {
  public:
    virtual ~IAuthImplementation() = default;

    virtual eAuthImplementations       getImplType()                         = 0;
    virtual void                       init()                                = 0;
    virtual bool                       isAuthenticated()                     = 0;
    virtual void                       handleInput(const std::string& input) = 0;
    virtual std::optional<std::string> getLastFailText()                     = 0;
    virtual std::optional<std::string> getLastPrompt()                       = 0;
    virtual bool                       checkWaiting()                        = 0;
    virtual void                       terminate()                           = 0;

    friend class CAuth;
};

class CAuth {
  public:
    CAuth();

    void                                 start();

    bool                                 isAuthenticated();
    void                                 submitInput(const std::string& input);
    std::optional<std::string>           getLastFailText();
    std::optional<std::string>           getLastPrompt();
    std::vector<int>                     getPollFDs();
    bool                                 checkWaiting();
    std::shared_ptr<IAuthImplementation> getImpl(const eAuthImplementations implType);

    void                                 terminate();

    // Should only be set via the main thread
    bool   m_bDisplayFailText = false;
    size_t m_iFailedAttempts  = 0;

    void   enqueueCheckAuthenticated();

  private:
    std::vector<std::shared_ptr<IAuthImplementation>> m_vImpls;
};

inline std::unique_ptr<CAuth> g_pAuth;
