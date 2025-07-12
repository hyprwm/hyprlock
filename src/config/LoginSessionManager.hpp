#pragma once

#include <string>
#include <vector>
#include "../defines.hpp"
#include "./ConfigDataValues.hpp"

class CLoginSessionManager {
  public:
    CLoginSessionManager()  = default;
    ~CLoginSessionManager() = default;

    CLoginSessionManager(const CLoginSessionManager&)            = delete;
    CLoginSessionManager& operator=(const CLoginSessionManager&) = delete;
    CLoginSessionManager(CLoginSessionManager&&) noexcept        = delete;

    void                                    gather(const std::string& sessionDirs);
    void                                    handleKeyUp();
    void                                    handleKeyDown();
    void                                    selectSession(size_t index);
    void                                    onGotLoginSessionAssetCallback();

    const SLoginSessionConfig&              getSelectedLoginSession() const;
    size_t                                  getSelectedLoginSessionIndex() const;
    const std::vector<std::string>&         getLoginSessionResourceIds() const;
    const std::vector<SLoginSessionConfig>& getLoginSessions() const;

  private:
    std::vector<SLoginSessionConfig> m_loginSessions;
    std::vector<std::string>         m_loginSessionResourceIds;
    size_t                           m_renderedSessionNames = 0;
    size_t                           m_selectedLoginSession = 0;

    bool                             m_fixedDefault = false;

    void                             requestSessionPickerAssets();
    void                             selectDefaultSession();
};

inline UP<CLoginSessionManager> g_pLoginSessionManager;
