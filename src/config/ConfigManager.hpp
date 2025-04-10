#pragma once

#include <hyprutils/animation/AnimationConfig.hpp>

#include <hyprlang.hpp>
#include <optional>
#include <vector>
#include <unordered_map>

#include "../defines.hpp"

class CConfigManager {
  public:
    CConfigManager(std::string configPath);
    void init();

    template <typename T>
    Hyprlang::CSimpleConfigValue<T> getValue(const std::string& name) {
        return Hyprlang::CSimpleConfigValue<T>(&m_config, name.c_str());
    }

    // Structure for widget configurations (background, shape, image, input-field, label)
    struct SWidgetConfig {
        std::string                               type;    // Widget type (e.g., "background")
        std::string                               monitor; // Monitor name (e.g., "DP-3")
        // Map of configuration values, e.g.:
        // For background: "path", "type", "color", "mpvpaper_mute", "mpvpaper_fps", etc.
        // For input-field: "size", "inner_color", etc.
        std::unordered_map<std::string, std::any> values;
    };

    std::vector<SWidgetConfig>                 getWidgetConfigs();

    std::optional<std::string>                 handleSource(const std::string&, const std::string&);
    std::optional<std::string>                 handleBezier(const std::string&, const std::string&);
    std::optional<std::string>                 handleAnimation(const std::string&, const std::string&);

    std::string                                configCurrentPath;

    Hyprutils::Animation::CAnimationConfigTree m_AnimationTree;

  private:
    Hyprlang::CConfig m_config;
};

inline UP<CConfigManager> g_pConfigManager;