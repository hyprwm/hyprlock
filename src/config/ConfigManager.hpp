#pragma once

#include <hyprutils/animation/AnimationConfig.hpp>

#include <hyprlang.hpp>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>

#include "../defines.hpp"

class CConfigManager {
  public:
    CConfigManager(std::string configPath);
    void         init();
    void* const* getValuePtr(const std::string& name);

    struct SWidgetConfig {
        std::string                               type;
        std::string                               monitor;

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

inline std::unique_ptr<CConfigManager> g_pConfigManager;
