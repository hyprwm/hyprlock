#pragma once

#include <hyprutils/animation/AnimatedVariable.hpp>

#include <hyprlang.hpp>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>

#include "../defines.hpp"

#define INITANIMCFG(name) m_mAnimationConfig[name] = makeShared<SAnimationPropertyConfig>();
#define CREATEANIMCFG(name, parent)                                                                                                                                                \
    RASSERT(m_mAnimationConfig[name], "Animation config \"{}\" has not been initialized", name);                                                                                   \
    *m_mAnimationConfig[name] = {false, "", "", 0.f, -1, m_mAnimationConfig["global"], m_mAnimationConfig[parent]};

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

    std::vector<SWidgetConfig>                         getWidgetConfigs();
    SP<Hyprutils::Animation::SAnimationPropertyConfig> getAnimationConfig(const std::string& name);

    std::optional<std::string>                         handleSource(const std::string&, const std::string&);
    std::optional<std::string>                         handleBezier(const std::string&, const std::string&);
    std::optional<std::string>                         handleAnimation(const std::string&, const std::string&);
    void                                               setAnimForChildren(SP<Hyprutils::Animation::SAnimationPropertyConfig> PANIM);

    std::string                                        configCurrentPath;

  private:
    Hyprlang::CConfig                                                                   m_config;

    std::unordered_map<std::string, SP<Hyprutils::Animation::SAnimationPropertyConfig>> m_mAnimationConfig;
};

inline std::unique_ptr<CConfigManager> g_pConfigManager;
