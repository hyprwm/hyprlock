#include "ConfigManager.hpp"
#include "ConfigDataValues.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../helpers/Log.hpp"
#include "../core/AnimationManager.hpp"
#include <hyprlang.hpp>
#include <hyprutils/string/String.hpp>
#include <hyprutils/path/Path.hpp>
#include <filesystem>
#include <glob.h>
#include <cstring>
#include <mutex>

using namespace Hyprutils::String;
using namespace Hyprutils::Animation;

ICustomConfigValueData::~ICustomConfigValueData() {
    ; // empty
}

static Hyprlang::CParseResult handleSource(const char* c, const char* v) {
    const std::string      VALUE   = v;
    const std::string      COMMAND = c;

    const auto             RESULT = g_pConfigManager->handleSource(COMMAND, VALUE);

    Hyprlang::CParseResult result;
    if (RESULT.has_value())
        result.setError(RESULT.value().c_str());
    return result;
}

static Hyprlang::CParseResult handleBezier(const char* c, const char* v) {
    const std::string      VALUE   = v;
    const std::string      COMMAND = c;

    const auto             RESULT = g_pConfigManager->handleBezier(COMMAND, VALUE);

    Hyprlang::CParseResult result;
    if (RESULT.has_value())
        result.setError(RESULT.value().c_str());
    return result;
}

static Hyprlang::CParseResult handleAnimation(const char* c, const char* v) {
    const std::string      VALUE   = v;
    const std::string      COMMAND = c;

    const auto             RESULT = g_pConfigManager->handleAnimation(COMMAND, VALUE);

    Hyprlang::CParseResult result;
    if (RESULT.has_value())
        result.setError(RESULT.value().c_str());
    return result;
}

static Hyprlang::CParseResult configHandleLayoutOption(const char* v, void** data) {
    const std::string      VALUE = v;

    Hyprlang::CParseResult result;

    if (!*data)
        *data = new CLayoutValueData();

    const auto DATA  = (CLayoutValueData*)(*data);
    const auto SPLIT = VALUE.find(',');
    if (SPLIT == std::string::npos) {
        result.setError(std::format("expected two comma seperated values, got {}", VALUE).c_str());
        return result;
    }

    auto lhs = VALUE.substr(0, SPLIT);
    auto rhs = VALUE.substr(SPLIT + 1);
    if (rhs.starts_with(" "))
        rhs = rhs.substr(1);

    if (lhs.contains(",") || rhs.contains(",")) {
        result.setError(std::format("too many arguments in {}", VALUE).c_str());
        return result;
    }

    if (lhs.ends_with("%")) {
        DATA->m_sIsRelative.x = true;
        lhs.pop_back();
    }

    if (rhs.ends_with("%")) {
        DATA->m_sIsRelative.y = true;
        rhs.pop_back();
    }

    DATA->m_vValues = Hyprutils::Math::Vector2D{std::stof(lhs), std::stof(rhs)};

    return result;
}

static void configHandleLayoutOptionDestroy(void** data) {
    if (*data)
        delete reinterpret_cast<CLayoutValueData*>(*data);
}

static Hyprlang::CParseResult configHandleGradientSet(const char* VALUE, void** data) {
    const std::string V = VALUE;

    if (!*data)
        *data = new CGradientValueData();

    const auto DATA = reinterpret_cast<CGradientValueData*>(*data);

    DATA->m_vColors.clear();
    DATA->m_bIsFallback = false;

    std::string parseError = "";
    std::string rolling    = V;

    while (!rolling.empty()) {
        const auto  SPACEPOS = rolling.find(' ');
        const bool  LAST     = SPACEPOS == std::string::npos;
        std::string var      = rolling.substr(0, SPACEPOS);
        if (var.find("rgb") != std::string::npos) { // rgb(a)
            const auto CLOSEPARENPOS = rolling.find(')');
            if (CLOSEPARENPOS == std::string::npos || CLOSEPARENPOS + 1 >= rolling.length()) {
                var = trim(rolling);
                rolling.clear();
            } else {
                var     = rolling.substr(0, CLOSEPARENPOS + 1);
                rolling = trim(rolling.substr(CLOSEPARENPOS + 2));
            }
        } else if (var.find("deg") != std::string::npos) { // last arg
            try {
                DATA->m_fAngle = std::stoi(var.substr(0, var.find("deg"))) * (M_PI / 180.0); // radians
            } catch (...) {
                Debug::log(WARN, "Error parsing gradient {}", V);
                parseError = "Error parsing gradient " + V;
            }

            break;
        } else // hex
            rolling = trim(rolling.substr(LAST ? rolling.length() : SPACEPOS + 1));

        if (DATA->m_vColors.size() >= 10) {
            Debug::log(WARN, "Error parsing gradient {}: max colors is 10.", V);
            parseError = "Error parsing gradient " + V + ": max colors is 10.";
            break;
        }

        if (var.empty())
            continue;

        try {
            DATA->m_vColors.emplace_back(configStringToInt(var));
        } catch (std::exception& e) {
            Debug::log(WARN, "Error parsing gradient {}", V);
            parseError = "Error parsing gradient " + V + ": " + e.what();
        }
    }

    if (V.empty()) {
        DATA->m_bIsFallback = true;
        DATA->m_vColors.emplace_back(0); // transparent
    }

    if (DATA->m_vColors.size() == 0) {
        Debug::log(WARN, "Error parsing gradient {}", V);
        parseError = "Error parsing gradient " + V + ": No colors?";

        DATA->m_vColors.emplace_back(0); // transparent
    }

    DATA->updateColorsOk();

    Hyprlang::CParseResult result;
    if (!parseError.empty())
        result.setError(parseError.c_str());

    return result;
}

static void configHandleGradientDestroy(void** data) {
    if (*data)
        delete reinterpret_cast<CGradientValueData*>(*data);
}

static std::string getMainConfigPath() {
    static const auto paths = Hyprutils::Path::findConfig("hyprlock");
    if (paths.first.has_value())
        return paths.first.value();
    else
        throw std::runtime_error("Could not find config in HOME, XDG_CONFIG_HOME, XDG_CONFIG_DIRS or /etc/hypr.");
}

CConfigManager::CConfigManager(std::string configPath) :
    m_config(configPath.empty() ? getMainConfigPath().c_str() : configPath.c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = configPath.empty()}) {
    configCurrentPath = configPath.empty() ? getMainConfigPath() : configPath;
}

inline static constexpr auto GRADIENTCONFIG = [](const char* default_value) -> Hyprlang::CUSTOMTYPE {
    return Hyprlang::CUSTOMTYPE{&configHandleGradientSet, configHandleGradientDestroy, default_value};
};

inline static constexpr auto LAYOUTCONFIG = [](const char* default_value) -> Hyprlang::CUSTOMTYPE {
    return Hyprlang::CUSTOMTYPE{&configHandleLayoutOption, configHandleLayoutOptionDestroy, default_value};
};

void CConfigManager::init() {

#define SHADOWABLE(name)                                                                                                                                                           \
    m_config.addSpecialConfigValue(name, "shadow_size", Hyprlang::INT{3});                                                                                                         \
    m_config.addSpecialConfigValue(name, "shadow_passes", Hyprlang::INT{0});                                                                                                       \
    m_config.addSpecialConfigValue(name, "shadow_color", Hyprlang::INT{0xFF000000});                                                                                               \
    m_config.addSpecialConfigValue(name, "shadow_boost", Hyprlang::FLOAT{1.2});

#define CLICKABLE(name) m_config.addSpecialConfigValue(name, "onclick", Hyprlang::STRING{""});

    m_config.addConfigValue("general:text_trim", Hyprlang::INT{1});
    m_config.addConfigValue("general:hide_cursor", Hyprlang::INT{0});
    m_config.addConfigValue("general:ignore_empty_input", Hyprlang::INT{0});
    m_config.addConfigValue("general:immediate_render", Hyprlang::INT{0});
    m_config.addConfigValue("general:fractional_scaling", Hyprlang::INT{2});
    m_config.addConfigValue("general:screencopy_mode", Hyprlang::INT{0});
    m_config.addConfigValue("general:fail_timeout", Hyprlang::INT{2000});

    m_config.addConfigValue("auth:pam:enabled", Hyprlang::INT{1});
    m_config.addConfigValue("auth:pam:module", Hyprlang::STRING{"hyprlock"});
    m_config.addConfigValue("auth:fingerprint:enabled", Hyprlang::INT{0});
    m_config.addConfigValue("auth:fingerprint:ready_message", Hyprlang::STRING{"(Scan fingerprint to unlock)"});
    m_config.addConfigValue("auth:fingerprint:present_message", Hyprlang::STRING{"Scanning fingerprint"});
    m_config.addConfigValue("auth:fingerprint:retry_delay", Hyprlang::INT{250});

    m_config.addConfigValue("animations:enabled", Hyprlang::INT{1});

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
    m_config.addSpecialConfigValue("background", "reload_time", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("background", "reload_cmd", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("background", "crossfade_time", Hyprlang::FLOAT{-1.0});

    m_config.addSpecialCategory("shape", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("shape", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("shape", "size", LAYOUTCONFIG("100,100"));
    m_config.addSpecialConfigValue("shape", "rounding", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "border_size", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "border_color", GRADIENTCONFIG("0xFF00CFE6"));
    m_config.addSpecialConfigValue("shape", "color", Hyprlang::INT{0xFF111111});
    m_config.addSpecialConfigValue("shape", "position", LAYOUTCONFIG("0,0"));
    m_config.addSpecialConfigValue("shape", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("shape", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("shape", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("shape", "xray", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("shape", "zindex", Hyprlang::INT{0});
    SHADOWABLE("shape");
    CLICKABLE("shape");

    m_config.addSpecialCategory("image", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("image", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "path", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "size", Hyprlang::INT{150});
    m_config.addSpecialConfigValue("image", "rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("image", "border_size", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("image", "border_color", GRADIENTCONFIG("0xFFDDDDDD"));
    m_config.addSpecialConfigValue("image", "position", LAYOUTCONFIG("0,0"));
    m_config.addSpecialConfigValue("image", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("image", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("image", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("image", "reload_time", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("image", "reload_cmd", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("image", "zindex", Hyprlang::INT{0});
    SHADOWABLE("image");
    CLICKABLE("image");

    m_config.addSpecialCategory("input-field", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("input-field", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("input-field", "size", LAYOUTCONFIG("400,90"));
    m_config.addSpecialConfigValue("input-field", "inner_color", Hyprlang::INT{0xFFDDDDDD});
    m_config.addSpecialConfigValue("input-field", "outer_color", GRADIENTCONFIG("0xFF111111"));
    m_config.addSpecialConfigValue("input-field", "outline_thickness", Hyprlang::INT{4});
    m_config.addSpecialConfigValue("input-field", "dots_size", Hyprlang::FLOAT{0.25});
    m_config.addSpecialConfigValue("input-field", "dots_center", Hyprlang::INT{1});
    m_config.addSpecialConfigValue("input-field", "dots_spacing", Hyprlang::FLOAT{0.2});
    m_config.addSpecialConfigValue("input-field", "dots_rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "dots_text_format", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("input-field", "fade_on_empty", Hyprlang::INT{1});
    m_config.addSpecialConfigValue("input-field", "fade_timeout", Hyprlang::INT{2000});
    m_config.addSpecialConfigValue("input-field", "font_color", Hyprlang::INT{0xFF000000});
    m_config.addSpecialConfigValue("input-field", "font_family", Hyprlang::STRING{"Sans"});
    m_config.addSpecialConfigValue("input-field", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("input-field", "position", LAYOUTCONFIG("0,0"));
    m_config.addSpecialConfigValue("input-field", "placeholder_text", Hyprlang::STRING{"<i>Input Password</i>"});
    m_config.addSpecialConfigValue("input-field", "hide_input", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "hide_input_base_color", Hyprlang::INT{0xEE00FF99});
    m_config.addSpecialConfigValue("input-field", "rounding", Hyprlang::INT{-1});
    m_config.addSpecialConfigValue("input-field", "check_color", GRADIENTCONFIG("0xFF22CC88"));
    m_config.addSpecialConfigValue("input-field", "fail_color", GRADIENTCONFIG("0xFFCC2222"));
    m_config.addSpecialConfigValue("input-field", "fail_text", Hyprlang::STRING{"<i>$FAIL</i>"});
    m_config.addSpecialConfigValue("input-field", "capslock_color", GRADIENTCONFIG(""));
    m_config.addSpecialConfigValue("input-field", "numlock_color", GRADIENTCONFIG(""));
    m_config.addSpecialConfigValue("input-field", "bothlock_color", GRADIENTCONFIG(""));
    m_config.addSpecialConfigValue("input-field", "invert_numlock", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "swap_font_color", Hyprlang::INT{0});
    m_config.addSpecialConfigValue("input-field", "zindex", Hyprlang::INT{0});
    SHADOWABLE("input-field");

    m_config.addSpecialCategory("label", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("label", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("label", "position", LAYOUTCONFIG("0,0"));
    m_config.addSpecialConfigValue("label", "color", Hyprlang::INT{0xFFFFFFFF});
    m_config.addSpecialConfigValue("label", "font_size", Hyprlang::INT{16});
    m_config.addSpecialConfigValue("label", "text", Hyprlang::STRING{"Sample Text"});
    m_config.addSpecialConfigValue("label", "font_family", Hyprlang::STRING{"Sans"});
    m_config.addSpecialConfigValue("label", "halign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("label", "valign", Hyprlang::STRING{"center"});
    m_config.addSpecialConfigValue("label", "rotate", Hyprlang::FLOAT{0});
    m_config.addSpecialConfigValue("label", "text_align", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("label", "zindex", Hyprlang::INT{0});
    SHADOWABLE("label");
    CLICKABLE("label");

    m_config.registerHandler(&::handleSource, "source", {.allowFlags = false});
    m_config.registerHandler(&::handleBezier, "bezier", {.allowFlags = false});
    m_config.registerHandler(&::handleAnimation, "animation", {.allowFlags = false});

    //
    // Init Animations
    //
    m_AnimationTree.createNode("global");

    // toplevel
    m_AnimationTree.createNode("fade", "global");
    m_AnimationTree.createNode("inputField", "global");

    // inputField
    m_AnimationTree.createNode("inputFieldColors", "inputField");
    m_AnimationTree.createNode("inputFieldFade", "inputField");
    m_AnimationTree.createNode("inputFieldWidth", "inputField");
    m_AnimationTree.createNode("inputFieldDots", "inputField");

    // fade
    m_AnimationTree.createNode("fadeIn", "fade");
    m_AnimationTree.createNode("fadeOut", "fade");

    // set config for root node
    m_AnimationTree.setConfigForNode("global", 1, 8.f, "default");
    m_AnimationTree.setConfigForNode("inputFieldColors", 1, 8.f, "linear");

    m_config.commence();

    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());

#undef SHADOWABLE
#undef CLICKABLE
}

std::vector<CConfigManager::SWidgetConfig> CConfigManager::getWidgetConfigs() {
    std::vector<CConfigManager::SWidgetConfig> result;

#define SHADOWABLE(name)                                                                                                                                                           \
    {"shadow_size", m_config.getSpecialConfigValue(name, "shadow_size", k.c_str())}, {"shadow_passes", m_config.getSpecialConfigValue(name, "shadow_passes", k.c_str())},          \
        {"shadow_color", m_config.getSpecialConfigValue(name, "shadow_color", k.c_str())}, {                                                                                       \
        "shadow_boost", m_config.getSpecialConfigValue(name, "shadow_boost", k.c_str())                                                                                            \
    }

#define CLICKABLE(name) {"onclick", m_config.getSpecialConfigValue(name, "onclick", k.c_str())}

    //
    auto keys = m_config.listKeysForSpecialCategory("background");
    result.reserve(keys.size());
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            .type = "background",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("background", "monitor", k.c_str())),
            .values = {
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
                {"reload_time", m_config.getSpecialConfigValue("background", "reload_time", k.c_str())},
                {"reload_cmd", m_config.getSpecialConfigValue("background", "reload_cmd", k.c_str())},
                {"crossfade_time", m_config.getSpecialConfigValue("background", "crossfade_time", k.c_str())},
            }
        });
        // clang-format on
    }

    //
    keys = m_config.listKeysForSpecialCategory("shape");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            .type = "shape",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("shape", "monitor", k.c_str())),
            .values = {
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
                CLICKABLE("shape"),
            }
        });
        // clang-format on
    }

    //
    keys = m_config.listKeysForSpecialCategory("image");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            .type = "image",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("image", "monitor", k.c_str())),
            .values = {
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
                CLICKABLE("image"),
            }
        });
        // clang-format on
    }

    keys = m_config.listKeysForSpecialCategory("input-field");
    for (auto& k : keys) {
        // clang-format off
        result.push_back(CConfigManager::SWidgetConfig{
            .type = "input-field",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("input-field", "monitor", k.c_str())),
            .values = {
                {"size", m_config.getSpecialConfigValue("input-field", "size", k.c_str())},
                {"inner_color", m_config.getSpecialConfigValue("input-field", "inner_color", k.c_str())},
                {"outer_color", m_config.getSpecialConfigValue("input-field", "outer_color", k.c_str())},
                {"outline_thickness", m_config.getSpecialConfigValue("input-field", "outline_thickness", k.c_str())},
                {"dots_size", m_config.getSpecialConfigValue("input-field", "dots_size", k.c_str())},
                {"dots_spacing", m_config.getSpecialConfigValue("input-field", "dots_spacing", k.c_str())},
                {"dots_center", m_config.getSpecialConfigValue("input-field", "dots_center", k.c_str())},
                {"dots_rounding", m_config.getSpecialConfigValue("input-field", "dots_rounding", k.c_str())},
                {"dots_text_format", m_config.getSpecialConfigValue("input-field", "dots_text_format", k.c_str())},
                {"fade_on_empty", m_config.getSpecialConfigValue("input-field", "fade_on_empty", k.c_str())},
                {"fade_timeout", m_config.getSpecialConfigValue("input-field", "fade_timeout", k.c_str())},
                {"font_color", m_config.getSpecialConfigValue("input-field", "font_color", k.c_str())},
                {"font_family", m_config.getSpecialConfigValue("input-field", "font_family", k.c_str())},
                {"halign", m_config.getSpecialConfigValue("input-field", "halign", k.c_str())},
                {"valign", m_config.getSpecialConfigValue("input-field", "valign", k.c_str())},
                {"position", m_config.getSpecialConfigValue("input-field", "position", k.c_str())},
                {"placeholder_text", m_config.getSpecialConfigValue("input-field", "placeholder_text", k.c_str())},
                {"hide_input", m_config.getSpecialConfigValue("input-field", "hide_input", k.c_str())},
                {"hide_input_base_color", m_config.getSpecialConfigValue("input-field", "hide_input_base_color", k.c_str())},
                {"rounding", m_config.getSpecialConfigValue("input-field", "rounding", k.c_str())},
                {"check_color", m_config.getSpecialConfigValue("input-field", "check_color", k.c_str())},
                {"fail_color", m_config.getSpecialConfigValue("input-field", "fail_color", k.c_str())},
                {"fail_text", m_config.getSpecialConfigValue("input-field", "fail_text", k.c_str())},
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
            .type = "label",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("label", "monitor", k.c_str())),
            .values = {
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
                CLICKABLE("label"),
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

std::optional<std::string> CConfigManager::handleBezier(const std::string& command, const std::string& args) {
    const auto  ARGS = CVarList(args);

    std::string bezierName = ARGS[0];

    if (ARGS[1] == "")
        return "too few arguments";
    float p1x = std::stof(ARGS[1]);

    if (ARGS[2] == "")
        return "too few arguments";
    float p1y = std::stof(ARGS[2]);

    if (ARGS[3] == "")
        return "too few arguments";
    float p2x = std::stof(ARGS[3]);

    if (ARGS[4] == "")
        return "too few arguments";
    float p2y = std::stof(ARGS[4]);

    if (ARGS[5] != "")
        return "too many arguments";

    g_pAnimationManager->addBezierWithName(bezierName, Vector2D(p1x, p1y), Vector2D(p2x, p2y));

    return {};
}

std::optional<std::string> CConfigManager::handleAnimation(const std::string& command, const std::string& args) {
    const auto ARGS = CVarList(args);

    const auto ANIMNAME = ARGS[0];

    if (!m_AnimationTree.nodeExists(ANIMNAME))
        return "no such animation";

    // This helper casts strings like "1", "true", "off", "yes"... to int.
    int64_t enabledInt = configStringToInt(ARGS[1]);

    // Checking that the int is 1 or 0 because the helper can return integers out of range.
    if (enabledInt > 1 || enabledInt < 0)
        return "invalid animation on/off state";

    if (!enabledInt) {
        m_AnimationTree.setConfigForNode(ANIMNAME, 0, 1, "default");
        return {};
    }

    int64_t speed = -1;

    // speed
    if (isNumber(ARGS[2], true)) {
        speed = std::stof(ARGS[2]);

        if (speed <= 0) {
            speed = 1.f;
            return "invalid speed";
        }
    } else {
        speed = 10.f;
        return "invalid speed";
    }

    std::string bezierName = ARGS[3];
    // ARGS[4] (style) currently usused by hyprlock
    m_AnimationTree.setConfigForNode(ANIMNAME, enabledInt, speed, bezierName, "");

    if (!g_pAnimationManager->bezierExists(bezierName)) {
        const auto PANIMNODE      = m_AnimationTree.getConfig(ANIMNAME);
        PANIMNODE->internalBezier = "default";
        return "no such bezier";
    }

    return {};
}
