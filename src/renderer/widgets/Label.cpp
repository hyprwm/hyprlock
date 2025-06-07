#include "Label.hpp"
#include "../Renderer.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../config/ConfigDataValues.hpp"
#include <hyprlang.hpp>
#include <stdexcept>

CLabel::~CLabel() {
    reset();
}

void CLabel::registerSelf(const std::shared_ptr<CLabel>& self) {
    m_self = self;
}

static void onTimer(std::weak_ptr<CLabel> ref) {
    if (auto PLABEL = ref.lock(); PLABEL) {
        // update label
        PLABEL->onTimerUpdate();
        // plant new timer
        PLABEL->plantTimer();
    }
}

static void onAssetCallback(std::weak_ptr<CLabel> ref) {
    if (auto PLABEL = ref.lock(); PLABEL)
        PLABEL->renderUpdate();
}

std::string CLabel::getUniqueResourceId() {
    return std::string{"label:"} + std::to_string((uintptr_t)this) + ",time:" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

void CLabel::onTimerUpdate() {
    std::string oldFormatted = label.formatted;

    label = formatString(labelPreFormat);

    if (label.formatted == oldFormatted && !label.alwaysUpdate)
        return;

    if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Trying to update label, but resource {} is still pending! Skipping update.", pendingResourceID);
        return;
    }

    // request new
    request.id        = getUniqueResourceId();
    pendingResourceID = request.id;
    request.asset     = label.formatted;

    request.callback = [REF = m_self]() { onAssetCallback(REF); };

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CLabel::plantTimer() {

    if (label.updateEveryMs != 0)
        labelTimer = g_pHyprlock->addTimer(std::chrono::milliseconds((int)label.updateEveryMs), [REF = m_self](auto, auto) { onTimer(REF); }, this, label.allowForceUpdate);
    else if (label.updateEveryMs == 0 && label.allowForceUpdate)
        labelTimer = g_pHyprlock->addTimer(std::chrono::hours(1), [REF = m_self](auto, auto) { onTimer(REF); }, this, true);
}

void CLabel::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    reset();

    outputStringPort = pOutput->stringPort;
    viewport         = pOutput->getViewport();

    shadow.configure(m_self.lock(), props, viewport);

    try {
        configPos      = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport);
        labelPreFormat = std::any_cast<Hyprlang::STRING>(props.at("text"));
        halign         = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign         = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        angle          = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));
        angle          = angle * M_PI / 180.0;
        onclickCommand = std::any_cast<Hyprlang::STRING>(props.at("onclick"));

        std::string textAlign  = std::any_cast<Hyprlang::STRING>(props.at("text_align"));
        std::string fontFamily = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
        CHyprColor  labelColor = std::any_cast<Hyprlang::INT>(props.at("color"));
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

        if (!textAlign.empty())
            request.props["text_align"] = textAlign;

    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CLabel: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CLabel: {}", e.what()); //
    }

    pos = configPos; // Label size not known yet

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    plantTimer();
}

void CLabel::reset() {
    if (g_pHyprlock->m_bTerminate)
        return;

    if (labelTimer) {
        labelTimer->cancel();
        labelTimer.reset();
    }

    if (asset)
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);

    asset = nullptr;
    pendingResourceID.clear();
    resourceID.clear();
}

bool CLabel::draw(const SRenderData& data) {
    if (!asset) {
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

        if (!asset)
            return true;
    }

    if (updateShadow) {
        updateShadow = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    // calc pos
    pos = posFromHVAlign(viewport, asset->texture.m_vSize, configPos, halign, valign, angle);

    CBox box = {pos.x, pos.y, asset->texture.m_vSize.x, asset->texture.m_vSize.y};
    box.rot  = angle;
    g_pRenderer->renderTexture(box, asset->texture, data.opacity);

    return false;
}

void CLabel::renderUpdate() {
    auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(pendingResourceID);
    if (newAsset) {
        // new asset is ready :D
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
        asset             = newAsset;
        resourceID        = pendingResourceID;
        pendingResourceID = "";
        updateShadow      = true;
    } else {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);

        g_pHyprlock->addTimer(std::chrono::milliseconds(100), [REF = m_self](auto, auto) { onAssetCallback(REF); }, nullptr);
        return;
    }

    g_pHyprlock->renderOutput(outputStringPort);
}

CBox CLabel::getBoundingBoxWl() const {
    if (!asset)
        return CBox{};

    return {
        Vector2D{pos.x, viewport.y - pos.y - asset->texture.m_vSize.y},
        asset->texture.m_vSize,
    };
}

void CLabel::onClick(uint32_t button, bool down, const Vector2D& pos) {
    if (down && !onclickCommand.empty())
        spawnAsync(onclickCommand);
}

void CLabel::onHover(const Vector2D& pos) {
    if (!onclickCommand.empty())
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
}
