#include "AnimationManager.hpp"
#include "../helpers/AnimatedVariable.hpp"
#include "../config/ConfigDataValues.hpp"
#include "../config/ConfigManager.hpp"

#include <utility>

CHyprlockAnimationManager::CHyprlockAnimationManager() {
    ;
}

template <Animable VarType>
void updateVariable(CAnimatedVariable<VarType>& av, const float POINTY, bool warp = false) {
    if (POINTY >= 1.f || warp || !av.enabled() || av.value() == av.goal()) {
        av.warp();
        return;
    }

    const auto DELTA = av.goal() - av.begun();
    av.value()       = av.begun() + DELTA * POINTY;
}

void updateColorVariable(CAnimatedVariable<CHyprColor>& av, const float POINTY, bool warp = false) {
    if (POINTY >= 1.f || warp || !av.enabled() || av.value() == av.goal()) {
        av.warp();
        return;
    }

    // convert both to OkLab, then lerp that, and convert back.
    // This is not as fast as just lerping rgb, but it's WAY more precise...
    // Use the CHyprColor cache for OkLab

    const auto&                L1 = av.begun().asOkLab();
    const auto&                L2 = av.goal().asOkLab();

    static const auto          lerp = [](const float one, const float two, const float progress) -> float { return one + (two - one) * progress; };

    const Hyprgraphics::CColor lerped = Hyprgraphics::CColor::SOkLab{
        .l = lerp(L1.l, L2.l, POINTY),
        .a = lerp(L1.a, L2.a, POINTY),
        .b = lerp(L1.b, L2.b, POINTY),
    };

    av.value() = {lerped, lerp(av.begun().a, av.goal().a, POINTY)};
}

void updateGradientVariable(CAnimatedVariable<CGradientValueData>& av, const float POINTY, bool warp = false) {
    if (POINTY >= 1.f || warp || av.value() == av.goal()) {
        av.warp();
        return;
    }

    av.value().m_vColors.resize(av.goal().m_vColors.size(), av.goal().m_vColors.back());

    for (size_t i = 0; i < av.value().m_vColors.size(); ++i) {
        const CHyprColor&          sourceCol = (i < av.begun().m_vColors.size()) ? av.begun().m_vColors[i] : av.begun().m_vColors.back();
        const CHyprColor&          targetCol = (i < av.goal().m_vColors.size()) ? av.goal().m_vColors[i] : av.goal().m_vColors.back();

        const auto&                L1 = sourceCol.asOkLab();
        const auto&                L2 = targetCol.asOkLab();

        static const auto          lerp = [](const float one, const float two, const float progress) -> float { return one + (two - one) * progress; };

        const Hyprgraphics::CColor lerped = Hyprgraphics::CColor::SOkLab{
            .l = lerp(L1.l, L2.l, POINTY),
            .a = lerp(L1.a, L2.a, POINTY),
            .b = lerp(L1.b, L2.b, POINTY),
        };

        av.value().m_vColors[i] = {lerped, lerp(sourceCol.a, targetCol.a, POINTY)};
        av.value().updateColorsOk();
    }

    if (av.begun().m_fAngle != av.goal().m_fAngle) {
        const float DELTA   = av.goal().m_fAngle - av.begun().m_fAngle;
        av.value().m_fAngle = av.begun().m_fAngle + DELTA * POINTY;
    }
}

void CHyprlockAnimationManager::tick() {
    static auto* const PANIMATIONSENABLED = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("animations:enabled");
    for (auto const& av : m_vActiveAnimatedVariables) {
        const auto PAV = av.lock();
        if (!PAV || !PAV->ok())
            continue;

        const auto SPENT   = PAV->getPercent();
        const auto PBEZIER = getBezier(PAV->getBezierName());
        const auto POINTY  = PBEZIER->getYForPoint(SPENT);

        switch (PAV->m_Type) {
            case AVARTYPE_FLOAT: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<float>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated float");
                updateVariable(*pTypedAV, POINTY, !**PANIMATIONSENABLED);
            } break;
            case AVARTYPE_VECTOR: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<Vector2D>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated Vector2D");
                updateVariable(*pTypedAV, POINTY, !**PANIMATIONSENABLED);
            } break;
            case AVARTYPE_COLOR: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<CHyprColor>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated CHyprColor");
                updateColorVariable(*pTypedAV, POINTY, !**PANIMATIONSENABLED);
            } break;
            case AVARTYPE_GRADIENT: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<CGradientValueData>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated CGradientValueData");
                updateGradientVariable(*pTypedAV, POINTY, !**PANIMATIONSENABLED);
            } break;
            default: continue;
        }

        av->onUpdate();
    }

    tickDone();
}

void CHyprlockAnimationManager::scheduleTick() {
    m_bTickScheduled = true;
}

void CHyprlockAnimationManager::onTicked() {
    m_bTickScheduled = false;
}
