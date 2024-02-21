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
    m_config.addConfigValue("general:hide_cursor", Hyprlang::INT{0});

    m_config.addSpecialCategory("background", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("background", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "path", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "color", Hyprlang::INT{0xFF111111});

    m_config.addSpecialCategory("input-field", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("input-field", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("input-field", "size", Hyprlang::VEC2{400, 90});
    m_config.addSpecialConfigValue("input-field", "inner_color", Hyprlang::INT{0xFFDDDDDD});
    m_config.addSpecialConfigValue("input-field", "outer_color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("input-field", "outline_thickness", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("input-field", "dots_size", Hyprlang::FLOAT{0.25});
    m_config.addSpecialConfigValue("input-field", "dots_spacing", Hyprlang::FLOAT{0.2});
    m_config.addSpecialConfigValue("input-field", "fade_on_empty", Hyprlang::INT{1});
    m_config.addSpecialConfigValue("input-field", "font_color", Hyprlang::INT{0xFF000000});
    m_config.addSpecialConfigValue("input-field", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "position", Hyprlang::VEC2{0, -20});
    m_config.addSpecialConfigValue("input-field", "placeholder_text", Hyprlang::STRING{"<i>Input Password</i>"});
    m_config.addSpecialConfigValue("input-field", "text_align", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "hide_input", Hyprlang::INT{0});

    m_config.addSpecialCategory("label", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("label", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("label", "position", Hyprlang::VEC2{400, 90});
    m_config.addSpecialConfigValue("label", "color", Hyprlang::INT{0xFFFFFFFF});
    m_config.addSpecialConfigValue("label", "font_size", Hyprlang::INT{16});
    m_config.addSpecialConfigValue("label", "text", Hyprlang::STRING{"Sample Text"});
    m_config.addSpecialConfigValue("label", "font_family", Hyprlang::STRING{"Sans"});
    m_config.addSpecialConfigValue("label", "halign", Hyprlang::STRING{"none"});
    m_config.addSpecialConfigValue("label", "valign", Hyprlang::STRING{"none"});

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
                {"color", m_config.getSpecialConfigValue("background", "color", k.c_str())},
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
                {"dots_size", m_config.getSpecialConfigValue("input-field", "dots_size", k.c_str())},
                {"dots_spacing", m_config.getSpecialConfigValue("input-field", "dots_spacing", k.c_str())},
                {"fade_on_empty", m_config.getSpecialConfigValue("input-field", "fade_on_empty", k.c_str())},
                {"font_color", m_config.getSpecialConfigValue("input-field", "font_color", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("input-field", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("input-field", "valign", k.c_str())},
                {"position", m_config.getSpecialConfigValue("input-field", "position", k.c_str())},
                {"placeholder_text", m_config.getSpecialConfigValue("input-field", "placeholder_text", k.c_str())},
                {"text_align", m_config.getSpecialConfigValue("input-field", "text_align", k.c_str())},
                {"hide_input", m_config.getSpecialConfigValue("input-field", "hide_input", k.c_str())},
            }
        });
        // clang-format on
    }

    keys = m_config.listKeysForSpecialCategory("label");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            "label",
            std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("label", "monitor", k.c_str())),
            {
                {"position", m_config.getSpecialConfigValue("label", "position", k.c_str())},
                {"color", m_config.getSpecialConfigValue("label", "color", k.c_str())},
                {"font_size", m_config.getSpecialConfigValue("label", "font_size", k.c_str())},
                {"font_family", m_config.getSpecialConfigValue("label", "font_family", k.c_str())},
                {"text", m_config.getSpecialConfigValue("label", "text", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("label", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("label", "valign", k.c_str())},
            }
        });
        // clang-format on
    }

    return result;
}
