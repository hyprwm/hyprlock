#pragma once

#include <string>
#include <vector>
#include "../defines.hpp"
#include "./ConfigDataValues.hpp"

class CLoginSessionManager {
  public:
    CLoginSessionManager(const std::string& sessionDirs);
    ~CLoginSessionManager() = default;

    CLoginSessionManager(const CLoginSessionManager&)            = delete;
    CLoginSessionManager& operator=(const CLoginSessionManager&) = delete;
    CLoginSessionManager(CLoginSessionManager&&) noexcept        = delete;

    void                                    handleKeyUp();
    void                                    handleKeyDown();
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
};

inline UP<CLoginSessionManager> g_pLoginSessionManager;
