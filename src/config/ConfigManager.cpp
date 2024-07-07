#include "ConfigManager.hpp"
#include "../helpers/MiscFunctions.hpp"
#include <filesystem>
#include <glob.h>
#include <cstring>
#include <mutex>

static Hyprlang::CParseResult handleSource(const char* c, const char* v) {
    const std::string      VALUE   = v;
    const std::string      COMMAND = c;

    const auto             RESULT = g_pConfigManager->handleSource(COMMAND, VALUE);

    Hyprlang::CParseResult result;
    if (RESULT.has_value())
        result.setError(RESULT.value().c_str());
    return result;
}

static std::string getConfigDir() {
    static const char* xdgConfigHome = getenv("XDG_CONFIG_HOME");

    if (xdgConfigHome && std::filesystem::path(xdgConfigHome).is_absolute())
        return xdgConfigHome;

    static const char* home = getenv("HOME");

    if (!home)
        throw std::runtime_error("Neither HOME nor XDG_CONFIG_HOME is set in the environment. Cannot determine config directory.");

    return home + std::string("/.config");
}

static std::string getMainConfigPath() {
    return getConfigDir() + "/hypr/hyprlock.conf";
}

CConfigManager::CConfigManager(std::string configPath) :
    m_config(configPath.empty() ? getMainConfigPath().c_str() : configPath.c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = configPath.empty()}) {
    configCurrentPath = configPath.empty() ? getMainConfigPath() : configPath;
}

void CConfigManager::init() {

#define SHADOWABLE(name)                                                                                                                                                           \
    m_config.addSpecialConfigValue(name, "shadow_size", Hyprlang::INT{3});                                                                                                         \
    m_config.addSpecialConfigValue(name, "shadow_passes", Hyprlang::INT{0});                                                                                                       \
    m_config.addSpecialConfigValue(name, "shadow_color", Hyprlang::INT{0xFF000000});                                                                                               \
    m_config.addSpecialConfigValue(name, "shadow_boost", Hyprlang::FLOAT{1.2});

    m_config.addConfigValue("general:disable_loading_bar", Hyprlang::INT{0});
    m_config.addConfigValue("general:text_trim", Hyprlang::INT{1});
    m_config.addConfigValue("general:hide_cursor", Hyprlang::INT{0});
    m_config.addConfigValue("general:grace", Hyprlang::INT{0});
    m_config.addConfigValue("general:no_fade_in", Hyprlang::INT{0});
    m_config.addConfigValue("general:no_fade_out", Hyprlang::INT{0});
    m_config.addConfigValue("general:ignore_empty_input", Hyprlang::INT{0});
    m_config.addConfigValue("general:pam_module", Hyprlang::STRING{"hyprlock"});

    m_config.addSpecialCategory("background", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("background", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "path", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("background", "blur_size", Hyprlang::INT{8});
    m_config.addSpecialConfigValue("background", "blur_passes", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("background", "noise", Hyprlang::FLOAT{0.0117});
    m_config.addSpecialConfigValue("background", "contrast", Hyprlang::FLOAT{0.8917});
    m_config.addSpecialConfigValue("background", "brightness", Hyprlang::FLOAT{0.8172});
    m_config.addSpecialConfigValue("background", "vibrancy", Hyprlang::FLOAT{0.1686});
    m_config.addSpecialConfigValue("background", "vibrancy_darkness", Hyprlang::FLOAT{0.05});
    m_config.addSpecialConfigValue("background", "zindex", Hyprlang::INT{-1});

    m_config.addSpecialCategory("shape", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("shape", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("shape", "size", Hyprlang::VEC2{100, 100});
    m_config.addSpecialConfigValue("shape", "rounding", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "border_size", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "border_color", Hyprlang::INT{0xFF00CFE6});
    m_config.addSpecialConfigValue("shape", "color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("shape", "position", Hyprlang::VEC2{0, 80});
    m_config.addSpecialConfigValue("shape", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("shape", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("shape", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("shape", "xray", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "zindex", Hyprlang::INT{0});
    SHADOWABLE("shape");

    m_config.addSpecialCategory("image", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("image", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "path", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "size", Hyprlang::INT{150});
    m_config.addSpecialConfigValue("image", "rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("image", "border_size", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("image", "border_color", Hyprlang::INT{0xFFDDDDDD});
    m_config.addSpecialConfigValue("image", "position", Hyprlang::VEC2{0, 200});
    m_config.addSpecialConfigValue("image", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("image", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("image", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("image", "reload_time", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("image", "reload_cmd", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "zindex", Hyprlang::INT{0});
    SHADOWABLE("image");

    m_config.addSpecialCategory("input-field", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("input-field", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("input-field", "size", Hyprlang::VEC2{400, 90});
    m_config.addSpecialConfigValue("input-field", "inner_color", Hyprlang::INT{0xFFDDDDDD});
    m_config.addSpecialConfigValue("input-field", "outer_color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("input-field", "outline_thickness", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("input-field", "dots_size", Hyprlang::FLOAT{0.25});
    m_config.addSpecialConfigValue("input-field", "dots_center", Hyprlang::INT{1});
    m_config.addSpecialConfigValue("input-field", "dots_spacing", Hyprlang::FLOAT{0.2});
    m_config.addSpecialConfigValue("input-field", "dots_rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "fade_on_empty", Hyprlang::INT{1});
    m_config.addSpecialConfigValue("input-field", "fade_timeout", Hyprlang::INT{2000});
    m_config.addSpecialConfigValue("input-field", "font_color", Hyprlang::INT{0xFF000000});
    m_config.addSpecialConfigValue("input-field", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "position", Hyprlang::VEC2{0, -20});
    m_config.addSpecialConfigValue("input-field", "placeholder_text", Hyprlang::STRING{"<i>Input Password</i>"});
    m_config.addSpecialConfigValue("input-field", "hide_input", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "check_color", Hyprlang::INT{0xFFCC8822});
    m_config.addSpecialConfigValue("input-field", "fail_color", Hyprlang::INT{0xFFCC2222});
    m_config.addSpecialConfigValue("input-field", "fail_text", Hyprlang::STRING{"<i>$FAIL</i>"});
    m_config.addSpecialConfigValue("input-field", "fail_timeout", Hyprlang::INT{2000});
    m_config.addSpecialConfigValue("input-field", "fail_transition", Hyprlang::INT{300});
    m_config.addSpecialConfigValue("input-field", "capslock_color", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "numlock_color", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "bothlock_color", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "invert_numlock", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "swap_font_color", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "zindex", Hyprlang::INT{0});
    SHADOWABLE("input-field");

    m_config.addSpecialCategory("label", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("label", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("label", "position", Hyprlang::VEC2{400, 90});
    m_config.addSpecialConfigValue("label", "color", Hyprlang::INT{0xFFFFFFFF});
    m_config.addSpecialConfigValue("label", "font_size", Hyprlang::INT{16});
    m_config.addSpecialConfigValue("label", "text", Hyprlang::STRING{"Sample Text"});
    m_config.addSpecialConfigValue("label", "font_family", Hyprlang::STRING{"Sans"});
    m_config.addSpecialConfigValue("label", "halign", Hyprlang::STRING{"none"});
    m_config.addSpecialConfigValue("label", "valign", Hyprlang::STRING{"none"});
    m_config.addSpecialConfigValue("label", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("label", "text_align", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("label", "zindex", Hyprlang::INT{0});
    SHADOWABLE("label");

    m_config.registerHandler(&::handleSource, "source", {false});

    m_config.commence();

    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());

#undef SHADOWABLE
}

std::mutex   configMtx;

void* const* CConfigManager::getValuePtr(const std::string& name) {
    std::lock_guard<std::mutex> lg(configMtx);
    return m_config.getConfigValuePtr(name.c_str())->getDataStaticPtr();
}

std::vector<CConfigManager::SWidgetConfig> CConfigManager::getWidgetConfigs() {
    std::vector<CConfigManager::SWidgetConfig> result;

#define SHADOWABLE(name)                                                                                                                                                           \
    {"shadow_size", m_config.getSpecialConfigValue(name, "shadow_size", k.c_str())}, {"shadow_passes", m_config.getSpecialConfigValue(name, "shadow_passes", k.c_str())},          \
        {"shadow_color", m_config.getSpecialConfigValue(name, "shadow_color", k.c_str())}, {                                                                                       \
        "shadow_boost", m_config.getSpecialConfigValue(name, "shadow_boost", k.c_str())                                                                                            \
    }

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
                {"blur_size", m_config.getSpecialConfigValue("background", "blur_size", k.c_str())},
                {"blur_passes", m_config.getSpecialConfigValue("background", "blur_passes", k.c_str())},
                {"noise", m_config.getSpecialConfigValue("background", "noise", k.c_str())},
                {"contrast", m_config.getSpecialConfigValue("background", "contrast", k.c_str())},
                {"vibrancy", m_config.getSpecialConfigValue("background", "vibrancy", k.c_str())},
                {"brightness", m_config.getSpecialConfigValue("background", "brightness", k.c_str())},
                {"vibrancy_darkness", m_config.getSpecialConfigValue("background", "vibrancy_darkness", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("background", "zindex", k.c_str())},
            }
        });
        // clang-format on
    }

    //
    keys = m_config.listKeysForSpecialCategory("shape");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            "shape",
            std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("shape", "monitor", k.c_str())),
            {
                {"size", m_config.getSpecialConfigValue("shape", "size", k.c_str())},
                {"rounding", m_config.getSpecialConfigValue("shape", "rounding", k.c_str())},
                {"border_size", m_config.getSpecialConfigValue("shape", "border_size", k.c_str())},
                {"border_color", m_config.getSpecialConfigValue("shape", "border_color", k.c_str())},
                {"color", m_config.getSpecialConfigValue("shape", "color", k.c_str())},
                {"position", m_config.getSpecialConfigValue("shape", "position", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("shape", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("shape", "valign", k.c_str())},
                {"rotate", m_config.getSpecialConfigValue("shape", "rotate", k.c_str())},
                {"xray", m_config.getSpecialConfigValue("shape", "xray", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("shape", "zindex", k.c_str())},
                SHADOWABLE("shape"),
            }
        });
        // clang-format on
    }

    //
    keys = m_config.listKeysForSpecialCategory("image");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            "image",
            std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("image", "monitor", k.c_str())),
            {
                {"path", m_config.getSpecialConfigValue("image", "path", k.c_str())},
                {"size", m_config.getSpecialConfigValue("image", "size", k.c_str())},
                {"rounding", m_config.getSpecialConfigValue("image", "rounding", k.c_str())},
                {"border_size", m_config.getSpecialConfigValue("image", "border_size", k.c_str())},
                {"border_color", m_config.getSpecialConfigValue("image", "border_color", k.c_str())},
                {"position", m_config.getSpecialConfigValue("image", "position", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("image", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("image", "valign", k.c_str())},
                {"rotate", m_config.getSpecialConfigValue("image", "rotate", k.c_str())},
                {"reload_time", m_config.getSpecialConfigValue("image", "reload_time", k.c_str())},
                {"reload_cmd", m_config.getSpecialConfigValue("image", "reload_cmd", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("image", "zindex", k.c_str())},
                SHADOWABLE("image"),
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
                {"dots_center", m_config.getSpecialConfigValue("input-field", "dots_center", k.c_str())},
                {"dots_rounding", m_config.getSpecialConfigValue("input-field", "dots_rounding", k.c_str())},
                {"fade_on_empty", m_config.getSpecialConfigValue("input-field", "fade_on_empty", k.c_str())},
                {"fade_timeout", m_config.getSpecialConfigValue("input-field", "fade_timeout", k.c_str())},
                {"font_color", m_config.getSpecialConfigValue("input-field", "font_color", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("input-field", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("input-field", "valign", k.c_str())},
                {"position", m_config.getSpecialConfigValue("input-field", "position", k.c_str())},
                {"placeholder_text", m_config.getSpecialConfigValue("input-field", "placeholder_text", k.c_str())},
                {"hide_input", m_config.getSpecialConfigValue("input-field", "hide_input", k.c_str())},
                {"rounding", m_config.getSpecialConfigValue("input-field", "rounding", k.c_str())},
                {"check_color", m_config.getSpecialConfigValue("input-field", "check_color", k.c_str())},
                {"fail_color", m_config.getSpecialConfigValue("input-field", "fail_color", k.c_str())},
                {"fail_text", m_config.getSpecialConfigValue("input-field", "fail_text", k.c_str())},
                {"fail_timeout", m_config.getSpecialConfigValue("input-field", "fail_timeout", k.c_str())},
                {"fail_transition", m_config.getSpecialConfigValue("input-field", "fail_transition", k.c_str())},
                {"capslock_color", m_config.getSpecialConfigValue("input-field", "capslock_color", k.c_str())},
                {"numlock_color", m_config.getSpecialConfigValue("input-field", "numlock_color", k.c_str())},
                {"bothlock_color", m_config.getSpecialConfigValue("input-field", "bothlock_color", k.c_str())},
                {"invert_numlock", m_config.getSpecialConfigValue("input-field", "invert_numlock", k.c_str())},
                {"swap_font_color", m_config.getSpecialConfigValue("input-field", "swap_font_color", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("input-field", "zindex", k.c_str())},
                SHADOWABLE("input-field"),
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
                {"rotate", m_config.getSpecialConfigValue("label", "rotate", k.c_str())},
                {"text_align", m_config.getSpecialConfigValue("label", "text_align", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("label", "zindex", k.c_str())},
                SHADOWABLE("label"),
            }
        });
        // clang-format on
    }

    return result;
}

std::optional<std::string> CConfigManager::handleSource(const std::string& command, const std::string& rawpath) {
    if (rawpath.length() < 2) {
        Debug::log(ERR, "source= path garbage");
        return "source path " + rawpath + " bogus!";
    }
    std::unique_ptr<glob_t, void (*)(glob_t*)> glob_buf{new glob_t, [](glob_t* g) { globfree(g); }};
    memset(glob_buf.get(), 0, sizeof(glob_t));

    const auto CURRENTDIR = std::filesystem::path(configCurrentPath).parent_path().string();

    if (auto r = glob(absolutePath(rawpath, CURRENTDIR).c_str(), GLOB_TILDE, nullptr, glob_buf.get()); r != 0) {
        std::string err = std::format("source= globbing error: {}", r == GLOB_NOMATCH ? "found no match" : GLOB_ABORTED ? "read error" : "out of memory");
        Debug::log(ERR, "{}", err);
        return err;
    }

    for (size_t i = 0; i < glob_buf->gl_pathc; i++) {
        const auto PATH = absolutePath(glob_buf->gl_pathv[i], CURRENTDIR);

        if (PATH.empty() || PATH == configCurrentPath) {
            Debug::log(WARN, "source= skipping invalid path");
            continue;
        }

        if (!std::filesystem::is_regular_file(PATH)) {
            if (std::filesystem::exists(PATH)) {
                Debug::log(WARN, "source= skipping non-file {}", PATH);
                continue;
            }

            Debug::log(ERR, "source= file doesnt exist");
            return "source file " + PATH + " doesn't exist!";
        }

        // allow for nested config parsing
        auto backupConfigPath = configCurrentPath;
        configCurrentPath     = PATH;

        m_config.parseFile(PATH.c_str());

        configCurrentPath = backupConfigPath;
    }

    return {};
}
