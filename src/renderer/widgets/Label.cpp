#include "Label.hpp"
#include "../Renderer.hpp"
#include "../AsyncResourceManager.hpp"
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

void CLabel::registerSelf(const ASP<CLabel>& self) {
    m_self = self;
}

static void onTimer(AWP<CLabel> ref) {
    if (auto PLABEL = ref.lock(); PLABEL) {
        // update label
        PLABEL->onTimerUpdate();
        // plant new timer
        PLABEL->plantTimer();
    }
}

void CLabel::onTimerUpdate() {
    std::string oldFormatted = label.formatted;

    label = formatString(labelPreFormat);

    if (label.formatted == oldFormatted && !label.alwaysUpdate)
        return;

    if (pendingResourceID > 0) {
        Debug::log(WARN, "Trying to update label, but resource {} is still pending! Skipping update.", pendingResourceID);
        return;
    }

    // request new
    request.text = label.formatted;

    AWP<IWidget> widget(m_self);
    pendingResourceID = (label.cmd) ? g_asyncResourceManager->requestTextCmd(request, widget.lock()) : g_asyncResourceManager->requestText(request, widget.lock());
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

    shadow.configure(m_self, props, viewport);

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

        request.text     = label.formatted;
        request.font     = fontFamily;
        request.fontSize = fontSize;
        request.color    = labelColor.asRGB();

        if (!textAlign.empty())
            request.align = parseTextAlignment(textAlign);

    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CLabel: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CLabel: {}", e.what()); //
    }

    pos = configPos; // Label size not known yet

    resourceID = (label.cmd) ? g_asyncResourceManager->requestTextCmd(request, nullptr) : g_asyncResourceManager->requestText(request, nullptr);

    plantTimer();
}

void CLabel::reset() {
    if (labelTimer) {
        labelTimer->cancel();
        labelTimer.reset();
    }

    if (g_pHyprlock->m_bTerminate)
        return;

    if (asset)
        g_asyncResourceManager->unload(asset);

    asset             = nullptr;
    pendingResourceID = 0;
    resourceID        = 0;
}

bool CLabel::draw(const SRenderData& data) {
    if (!asset) {
        asset = g_asyncResourceManager->getAssetByID(resourceID);

        if (!asset)
            return true;
    }

    if (updateShadow) {
        updateShadow = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    // calc pos
    pos = posFromHVAlign(viewport, asset->m_vSize, configPos, halign, valign, angle);

    CBox box = {pos.x, pos.y, asset->m_vSize.x, asset->m_vSize.y};
    box.rot  = angle;
    g_pRenderer->renderTexture(box, *asset, data.opacity);

    return false;
}

void CLabel::onAssetUpdate(ASP<CTexture> newAsset) {
    pendingResourceID = 0;

    if (!newAsset)
        Debug::log(ERR, "asset update failed, resourceID: {} not available on update!", pendingResourceID);
    else if (newAsset->m_iType == TEXTURE_INVALID) {
        g_asyncResourceManager->unload(newAsset);
        Debug::log(ERR, "New image asset has an invalid texture!");
    } else {
        // new asset is ready :D
        g_asyncResourceManager->unload(asset);
        asset        = newAsset;
        resourceID   = pendingResourceID;
        updateShadow = true;

        g_pHyprlock->renderOutput(outputStringPort);
    }
}

CBox CLabel::getBoundingBoxWl() const {
    if (!asset)
        return CBox{};

    return {
        Vector2D{pos.x, viewport.y - pos.y - asset->m_vSize.y},
        asset->m_vSize,
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
