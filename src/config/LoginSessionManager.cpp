#include "./LoginSessionManager.hpp"
#include "./ConfigManager.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../helpers/Log.hpp"
#include "../renderer/AsyncResourceGatherer.hpp"
#include "../renderer/Renderer.hpp"

#include <fstream>
#include <filesystem>
#include <hyprutils/string/VarList.hpp>

static bool parseDesktopFile(SLoginSessionConfig& sessionConfig) {
    RASSERT(!sessionConfig.desktopFilePath.empty(), "Desktop file path is empty");

    // read line for line and parse the desktop file naivly
    std::ifstream fHandle(sessionConfig.desktopFilePath.c_str());
    std::string   line;
    try {
        while (std::getline(fHandle, line)) {
            if (line.empty())
                continue;

            if (line.find("Name=") != std::string::npos)
                sessionConfig.name = line.substr(5);
            else if (line.find("Exec=") != std::string::npos)
                sessionConfig.exec = line.substr(5);

            if (!sessionConfig.name.empty() && !sessionConfig.exec.empty())
                break;
        }
    } catch (const std::ifstream::failure& e) {
        Debug::log(ERR, "Failed to read session file {}: {}", sessionConfig.desktopFilePath.c_str(), e.what());
        return false;
    }

    if (sessionConfig.name.empty() || sessionConfig.exec.empty()) {
        Debug::log(ERR, "Failed to parse session file {}: missing name or exec", sessionConfig.desktopFilePath.c_str());
        return false;
    }

    return true;
}

static std::vector<SLoginSessionConfig> gatherSessionsInPaths(const std::vector<std::string>& searchPaths) {
    std::vector<SLoginSessionConfig> sessions;

    for (const auto& DIR : searchPaths) {
        if (!std::filesystem::exists(DIR))
            continue;

        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator{DIR}) {
            if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".desktop")
                continue;

            SLoginSessionConfig session;
            session.desktopFilePath = absolutePath(dirEntry.path().filename(), DIR);

            if (!parseDesktopFile(session))
                continue;

            sessions.emplace_back(session);
        }
    }

    return sessions;
}

void CLoginSessionManager::selectDefaultSession() {
    const auto        PDEFAULTSESSION = g_pConfigManager->getValue<Hyprlang::STRING>("login:default_session");
    const std::string DEFAULTSESSION{*PDEFAULTSESSION};
    if (DEFAULTSESSION.empty())
        return;

    Debug::log(LOG, "Selecting default session: {}", DEFAULTSESSION);

    bool found = false;

    if (DEFAULTSESSION.contains(".desktop")) {
        // default session is a path, check if we already have it parsed.

        const auto ABSPATH = absolutePath(DEFAULTSESSION, "/");
        for (size_t i = 0; i < m_loginSessions.size(); i++) {
            if (m_loginSessions[i].desktopFilePath == ABSPATH) {
                m_selectedLoginSession = i;
                found                  = true;
                break;
            }
        }

        if (!found) {
            // default session is a path, but not contained in sessionDirs
            SLoginSessionConfig defaultSession;
            defaultSession.desktopFilePath = ABSPATH;
            if (parseDesktopFile(defaultSession)) {
                m_loginSessions.insert(m_loginSessions.begin(), defaultSession);
                m_selectedLoginSession = 0;
                found                  = true;
            }
        }
    } else {
        // default session is a name
        for (size_t i = 0; i < m_loginSessions.size(); i++) {
            if (m_loginSessions[i].name == DEFAULTSESSION) {
                m_selectedLoginSession = i;
                found                  = true;
                break;
            }
        }
    }

    if (!found)
        Debug::log(WARN, "[GreetdLogin] Default session {} not found", DEFAULTSESSION);
}

void CLoginSessionManager::gather(const std::string& sessionDirs) {
    Debug::log(LOG, "[GreetdLogin] Search directories: {}", sessionDirs);

    Hyprutils::String::CVarList sessionDirPaths{sessionDirs, 0, ':', true};
    m_loginSessions = gatherSessionsInPaths(std::vector<std::string>{sessionDirPaths.begin(), sessionDirPaths.end()});

    const auto CONFIGUEDSESSIONS = g_pConfigManager->getLoginSessionConfigs();
    m_loginSessions.insert(m_loginSessions.end(), CONFIGUEDSESSIONS.begin(), CONFIGUEDSESSIONS.end());

    selectDefaultSession();

    if (m_loginSessions.empty()) {
        Debug::log(CRIT,
                   "[GreetdLogin] Hyprlock did not find any wayland sessions.\n"
                   "By default, hyprlock searches /usr/share/wayland-sessions and /usr/local/share/wayland-sessions.\n"
                   "You can specify the directories hyprlock searches in with the --session-dirs argument followed by a comma seperated list of directories.\n"
                   "Alternatively, you can specify a session with the login-session hyprlock keyword. Read the wiki for more info\n");

        m_loginSessions.emplace_back(SLoginSessionConfig{
            .name = "Hyprland",
            .exec = "/usr/bin/Hyprland",
        });
    }

    if (!g_pConfigManager->widgetsContainSessionPicker())
        m_fixedDefault = true;

    requestSessionPickerAssets();
}

void CLoginSessionManager::handleKeyUp() {
    if (m_fixedDefault)
        return;

    if (m_loginSessions.size() > 1) {
        if (m_selectedLoginSession > 0)
            m_selectedLoginSession--;
        else
            m_selectedLoginSession = m_loginSessions.size() - 1;
    }
}

void CLoginSessionManager::handleKeyDown() {
    if (m_fixedDefault)
        return;

    if (m_loginSessions.size() > 1) {
        m_selectedLoginSession++;
        if (m_selectedLoginSession >= m_loginSessions.size())
            m_selectedLoginSession = 0;
    }
}

void CLoginSessionManager::selectSession(size_t index) {
    if (index < m_loginSessions.size())
        m_selectedLoginSession = index;
}

void CLoginSessionManager::onGotLoginSessionAssetCallback() {
    m_renderedSessionNames++;
    if (m_renderedSessionNames == m_loginSessionResourceIds.size())
        g_pHyprlock->renderAllOutputs();
}

const SLoginSessionConfig& CLoginSessionManager::getSelectedLoginSession() const {
    return m_loginSessions[m_selectedLoginSession];
}

size_t CLoginSessionManager::getSelectedLoginSessionIndex() const {
    return m_selectedLoginSession;
}

const std::vector<SLoginSessionConfig>& CLoginSessionManager::getLoginSessions() const {
    return m_loginSessions;
}

const std::vector<std::string>& CLoginSessionManager::getLoginSessionResourceIds() const {
    return m_loginSessionResourceIds;
}

static void sessionNameAssetCallback() {
    g_pLoginSessionManager->onGotLoginSessionAssetCallback();
}

void CLoginSessionManager::requestSessionPickerAssets() {
    m_loginSessionResourceIds = std::vector<std::string>{m_loginSessions.size(), ""};
    for (size_t i = 0; i < m_loginSessions.size(); ++i) {
        const auto& SESSIONCONFIG    = m_loginSessions[i];
        m_loginSessionResourceIds[i] = std::format("session:{}-{}", (uintptr_t)this, SESSIONCONFIG.name);

        // request asset preload
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id    = m_loginSessionResourceIds[i];
        request.asset = SESSIONCONFIG.name;
        request.type  = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        //request.props["font_family"] = fontFamily;
        //request.props["color"]     = colorConfig.font;
        //request.props["font_size"] = rowHeight;
        request.callback = sessionNameAssetCallback;

        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }
}
