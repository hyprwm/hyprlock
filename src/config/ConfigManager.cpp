#include "ConfigManager.hpp"
#include <filesystem>

static std::string getConfigDir() {
    static const char* xdgConfigHome = getenv("XDG_CONFIG_HOME");

    if (xdgConfigHome && std::filesystem::path(xdgConfigHome).is_absolute())
        return xdgConfigHome;

    return getenv("HOME") + std::string("/.config");
}

static std::string getMainConfigPath() {
    return getConfigDir() + "/hypr/hyprlock.conf";
}

CConfigManager::CConfigManager() : m_config(getMainConfigPath().c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = true}) {
    ;
}

void CConfigManager::init() {
    m_config.addConfigValue("general:disable_loading_bar", Hyprlang::INT{0});

    m_config.addSpecialCategory("background", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("background", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "path", Hyprlang::STRING{""});

    m_config.addSpecialCategory("input-field", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("input-field", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("input-field", "size", Hyprlang::VEC2{400, 90});
    m_config.addSpecialConfigValue("input-field", "inner_color", Hyprlang::INT{0xFFDDDDDD});
    m_config.addSpecialConfigValue("input-field", "outer_color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("input-field", "outline_thickness", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("input-field", "fade_on_empty", Hyprlang::INT{1});

    m_config.commence();

    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());
}

std::mutex   configMtx;

void* const* CConfigManager::getValuePtr(const std::string& name) {
    std::lock_guard<std::mutex> lg(configMtx);
    return m_config.getConfigValuePtr(name.c_str())->getDataStaticPtr();
}

std::vector<CConfigManager::SWidgetConfig> CConfigManager::getWidgetConfigs() {
    std::vector<CConfigManager::SWidgetConfig> result;

    //
    auto keys = m_config.listKeysForSpecialCategory("background");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            "background",
            std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("background", "monitor", k.c_str())),
            {
                {"path", m_config.getSpecialConfigValue("background", "path", k.c_str())},
            }
        });
        // clang-format on
    }

    keys = m_config.listKeysForSpecialCategory("input-field");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            "input-field",
            std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("input-field", "monitor", k.c_str())),
            {
                {"size", m_config.getSpecialConfigValue("input-field", "size", k.c_str())},
                {"inner_color", m_config.getSpecialConfigValue("input-field", "inner_color", k.c_str())},
                {"outer_color", m_config.getSpecialConfigValue("input-field", "outer_color", k.c_str())},
                {"outline_thickness", m_config.getSpecialConfigValue("input-field", "outline_thickness", k.c_str())},
                {"fade_on_empty", m_config.getSpecialConfigValue("input-field", "fade_on_empty", k.c_str())},
            }
        });
        // clang-format on
    }

    return result;
}