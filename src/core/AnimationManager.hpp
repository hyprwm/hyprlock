#pragma once

#include <hyprutils/animation/AnimationManager.hpp>
#include <hyprutils/animation/AnimatedVariable.hpp>

#include "../helpers/AnimatedVariable.hpp"
#include "../defines.hpp"

class CHyprlockAnimationManager : public Hyprutils::Animation::CAnimationManager {
  public:
    CHyprlockAnimationManager();

    void         tick();
    virtual void scheduleTick();
    virtual void onTicked();

    using SAnimationPropertyConfig = Hyprutils::Animation::SAnimationPropertyConfig;

    template <Animable VarType>
    void createAnimation(const VarType& v, PHLANIMVAR<VarType>& pav, SP<SAnimationPropertyConfig> pConfig) {
        constexpr const eAnimatedVarType EAVTYPE = typeToeAnimatedVarType<VarType>;
        pav                                      = makeUnique<CAnimatedVariable<VarType>>();

        pav->create(EAVTYPE, static_cast<Hyprutils::Animation::CAnimationManager*>(this), pav, v);
        pav->setConfig(pConfig);
    }

    bool m_bTickScheduled = false;
};

inline UP<CHyprlockAnimationManager> g_pAnimationManager;
