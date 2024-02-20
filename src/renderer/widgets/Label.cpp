#include "Label.hpp"
#include "../../helpers/Color.hpp"
#include <hyprlang.hpp>
#include "../Renderer.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/hyprlock.hpp"

CLabel::~CLabel() {
    labelTimer->cancel();
    labelTimer.reset();
}

static void onTimer(std::shared_ptr<CTimer> self, void* data) {
    const auto PLABEL = (CLabel*)data;

    // update label
    PLABEL->onTimerUpdate();

    // render and replant
    PLABEL->renderSuper();
    PLABEL->plantTimer();
}

void CLabel::onTimerUpdate() {
    std::string oldFormatted = label.formatted;

    label = formatString(labelPreFormat);

    if (label.formatted == oldFormatted)
        return;

    if (!pendingResourceID.empty())
        return; // too many updates, we'll miss some. Shouldn't happen tbh

    // request new
    request.id        = std::string{"label:"} + std::to_string((uintptr_t)this) + ",time:" + std::to_string(time(nullptr));
    pendingResourceID = request.id;
    request.asset     = label.formatted;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CLabel::plantTimer() {
    if (label.updateEveryMs != 0)
        labelTimer = g_pHyprlock->addTimer(std::chrono::milliseconds((int)label.updateEveryMs), onTimer, this);
}

CLabel::CLabel(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props, CSessionLockSurface* surface_) : surface(surface_) {
    labelPreFormat         = std::any_cast<Hyprlang::STRING>(props.at("text"));
    std::string fontFamily = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
    CColor      labelColor = std::any_cast<Hyprlang::INT>(props.at("color"));
    int         fontSize   = std::any_cast<Hyprlang::INT>(props.at("font_size"));

    label = formatString(labelPreFormat);

    request.id                   = std::string{"label:"} + std::to_string((uintptr_t)this) + ",time:" + std::to_string(time(nullptr));
    resourceID                   = request.id;
    request.asset                = label.formatted;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = labelColor;
    request.props["font_size"]   = fontSize;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    auto POS__ = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    pos        = {POS__.x, POS__.y};
    configPos  = pos;

    viewport = viewport_;

    halign = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign = std::any_cast<Hyprlang::STRING>(props.at("valign"));

    plantTimer();
}

bool CLabel::draw(const SRenderData& data) {
    if (!asset) {
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

        if (!asset)
            return true;

        // calc pos
        pos = posFromHVAlign(viewport, asset->texture.m_vSize, configPos, halign, valign);
    }

    if (!pendingResourceID.empty()) {
        // new asset is pending
        auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(pendingResourceID);
        if (newAsset) {
            // new asset is ready :D
            g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
            asset             = newAsset;
            resourceID        = pendingResourceID;
            pendingResourceID = "";
            pos               = posFromHVAlign(viewport, asset->texture.m_vSize, configPos, halign, valign);
        }
    }

    CBox box = {pos.x, pos.y, asset->texture.m_vSize.x, asset->texture.m_vSize.y};

    g_pRenderer->renderTexture(box, asset->texture, data.opacity);

    return false;
}

void CLabel::renderSuper() {
    surface->render();
}