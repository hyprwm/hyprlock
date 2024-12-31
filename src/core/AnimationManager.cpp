#include "AnimationManager.hpp"
#include "../helpers/AnimatedVariable.hpp"

#include <utility>

CHyprlockAnimationManager::CHyprlockAnimationManager() {
    ;
}

template <Animable VarType>
void updateVariable(CAnimatedVariable<VarType>& av, const float POINTY, bool warp = false) {
    if (POINTY >= 1.f || warp || av.value() == av.goal()) {
        av.warp();
        return;
    }

    const auto DELTA = av.goal() - av.begun();
    av.value()       = av.begun() + DELTA * POINTY;
}

void updateColorVariable(CAnimatedVariable<CHyprColor>& av, const float POINTY, bool warp = false) {
    if (POINTY >= 1.f || warp || av.value() == av.goal()) {
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

void CHyprlockAnimationManager::tick() {
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
                updateVariable(*pTypedAV, POINTY);
            } break;
            case AVARTYPE_VECTOR: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<Vector2D>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated Vector2D");
                updateVariable(*pTypedAV, POINTY);
            } break;
            case AVARTYPE_COLOR: {
                auto pTypedAV = dynamic_cast<CAnimatedVariable<CHyprColor>*>(PAV.get());
                RASSERT(pTypedAV, "Failed to upcast animated CHyprColor");
                updateColorVariable(*pTypedAV, POINTY);
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
