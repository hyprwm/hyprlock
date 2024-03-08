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

    // plant new timer
    PLABEL->plantTimer();
}

static void onAssetCallback(void* data) {
    const auto PLABEL = (CLabel*)data;
    PLABEL->renderSuper();
}

std::string CLabel::getUniqueResourceId() {
    return std::string{"label:"} + std::to_string((uintptr_t)this) + ",time:" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

void CLabel::onTimerUpdate() {
    std::string oldFormatted = label.formatted;

    label = formatString(labelPreFormat);

    if (label.formatted == oldFormatted && !label.alwaysUpdate)
        return;

    if (!pendingResourceID.empty())
        return; // too many updates, we'll miss some. Shouldn't happen tbh

    // request new
    request.id        = getUniqueResourceId();
    pendingResourceID = request.id;
    request.asset     = label.formatted;

    request.callback     = onAssetCallback;
    request.callbackData = this;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CLabel::plantTimer() {
    if (label.updateEveryMs != 0)
        labelTimer = g_pHyprlock->addTimer(std::chrono::milliseconds((int)label.updateEveryMs), onTimer, this);
}

CLabel::CLabel(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props, const std::string& output) :
    outputStringPort(output), shadow(this, props, viewport_) {
    labelPreFormat         = std::any_cast<Hyprlang::STRING>(props.at("text"));
    std::string fontFamily = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
    CColor      labelColor = std::any_cast<Hyprlang::INT>(props.at("color"));
    int         fontSize   = std::any_cast<Hyprlang::INT>(props.at("font_size"));

    label = formatString(labelPreFormat);

    request.id                   = getUniqueResourceId();
    resourceID                   = request.id;
    request.asset                = label.formatted;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = labelColor;
    request.props["font_size"]   = fontSize;
    request.props["cmd"]         = label.cmd;

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
        shadow.markShadowDirty();
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
            shadow.markShadowDirty();
        }
    }

    CBox box = {pos.x, pos.y, asset->texture.m_vSize.x, asset->texture.m_vSize.y};
    shadow.draw(data);
    g_pRenderer->renderTexture(box, asset->texture, data.opacity);

    return !pendingResourceID.empty();
}

static void onAssetCallbackTimer(std::shared_ptr<CTimer> self, void* data) {
    const auto PLABEL = (CLabel*)data;
    PLABEL->renderSuper();
}

void CLabel::renderSuper() {
    const auto MON =
        std::find_if(g_pHyprlock->m_vOutputs.begin(), g_pHyprlock->m_vOutputs.end(), [this](const auto& other) { return other->stringPort == this->outputStringPort; });

    if (MON == g_pHyprlock->m_vOutputs.end() || !MON->get())
        return;

    const auto PMONITOR = MON->get();

    PMONITOR->sessionLockSurface->render();

    if (!pendingResourceID.empty()) /* did not consume the pending resource */
        g_pHyprlock->addTimer(std::chrono::milliseconds(100), onAssetCallbackTimer, this);
}