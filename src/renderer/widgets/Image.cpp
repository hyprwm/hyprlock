#include "Image.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../config/ConfigDataValues.hpp"
#include <cmath>
#include <hyprlang.hpp>
#include <hyprutils/math/Vector2D.hpp>

CImage::~CImage() {
    reset();
}

void CImage::registerSelf(const ASP<CImage>& self) {
    m_self = self;
}

static void onTimer(AWP<CImage> ref) {
    if (auto PIMAGE = ref.lock(); PIMAGE) {
        PIMAGE->onTimerUpdate();
        PIMAGE->plantTimer();
    }
}

static void onAssetCallback(AWP<CImage> ref) {
    if (auto PIMAGE = ref.lock(); PIMAGE)
        PIMAGE->renderUpdate();
}

void CImage::onTimerUpdate() {
    const std::string OLDPATH = path;

    if (!reloadCommand.empty()) {
        path = spawnSync(reloadCommand);

        if (path.ends_with('\n'))
            path.pop_back();

        if (path.starts_with("file://"))
            path = path.substr(7);

        if (path.empty())
            return;
    }

    try {
        const auto MTIME = std::filesystem::last_write_time(absolutePath(path, ""));
        if (OLDPATH == path && MTIME == modificationTime)
            return;

        modificationTime = MTIME;
    } catch (std::exception& e) {
        path = OLDPATH;
        Debug::log(ERR, "{}", e.what());
        return;
    }

    if (!pendingResourceID.empty())
        return;

    request.id        = std::string{"image:"} + path + ",time:" + std::to_string((uint64_t)modificationTime.time_since_epoch().count());
    pendingResourceID = request.id;
    request.asset     = path;
    request.type      = CAsyncResourceGatherer::eTargetType::TARGET_IMAGE;
    request.callback  = [REF = m_self]() { onAssetCallback(REF); };
    request.props["size"] = Vector2D{(double)size, (double)size};

    g_pAsyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CImage::plantTimer() {

    if (reloadTime == 0) {
        imageTimer = g_pHyprlock->addTimer(std::chrono::hours(1), [REF = m_self](auto, auto) { onTimer(REF); }, nullptr, true);
    } else if (reloadTime > 0)
        imageTimer = g_pHyprlock->addTimer(std::chrono::seconds(reloadTime), [REF = m_self](auto, auto) { onTimer(REF); }, nullptr, false);
}

void CImage::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    reset();

    viewport   = pOutput->getViewport();
    stringPort = pOutput->stringPort;

    shadow.configure(m_self, props, viewport);

    try {
        size      = std::any_cast<Hyprlang::INT>(props.at("size"));
        rounding  = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        border    = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        color     = *CGradientValueData::fromAnyPv(props.at("border_color"));
        configPos = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport);
        halign    = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign    = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        angle     = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));

        path           = std::any_cast<Hyprlang::STRING>(props.at("path"));
        reloadTime     = std::any_cast<Hyprlang::INT>(props.at("reload_time"));
        reloadCommand  = std::any_cast<Hyprlang::STRING>(props.at("reload_cmd"));
        onclickCommand = std::any_cast<Hyprlang::STRING>(props.at("onclick"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CImage: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing propperty for CImage: {}", e.what()); //
    }

    resourceID = "image:" + path;
    angle      = angle * M_PI / 180.0;

    if (reloadTime > -1) {
        try {
            modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
        } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }

        plantTimer();
    }
}

void CImage::reset() {
    if (imageTimer) {
        imageTimer->cancel();
        imageTimer.reset();
    }

    if (g_pHyprlock->m_bTerminate)
        return;

    imageFB.destroyBuffer();

    if (asset && reloadTime > -1) // Don't unload asset if it's a static image
        g_pAsyncResourceGatherer->unloadAsset(asset);

    asset             = nullptr;
    pendingResourceID = "";
    resourceID        = "";
}

bool CImage::draw(const SRenderData& data) {

    if (resourceID.empty())
        return false;

    if (!asset)
        asset = g_pAsyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return true;

    if (asset->texture.m_iType == TEXTURE_INVALID) {
        g_pAsyncResourceGatherer->unloadAsset(asset);
        resourceID = "";
        return false;
    }

    if (!imageFB.isAllocated()) {

        const Vector2D IMAGEPOS  = {border, border};
        const Vector2D BORDERPOS = {0.0, 0.0};
        const Vector2D TEXSIZE   = asset->texture.m_vSize;
        const float    SCALEX    = size / TEXSIZE.x;
        const float    SCALEY    = size / TEXSIZE.y;

        // image with borders offset, with extra pixel for anti-aliasing when rotated
        CBox texbox = {angle == 0 ? IMAGEPOS : IMAGEPOS + Vector2D{1.0, 1.0}, TEXSIZE};

        texbox.w *= std::max(SCALEX, SCALEY);
        texbox.h *= std::max(SCALEX, SCALEY);

        // plus borders if any
        CBox borderBox = {angle == 0 ? BORDERPOS : BORDERPOS + Vector2D{1.0, 1.0}, texbox.size() + IMAGEPOS * 2.0};

        borderBox.round();

        const Vector2D FBSIZE      = angle == 0 ? borderBox.size() : borderBox.size() + Vector2D{2.0, 2.0};
        const int      ROUND       = roundingForBox(texbox, rounding);
        const int      BORDERROUND = roundingForBorderBox(borderBox, rounding, border);

        imageFB.alloc(FBSIZE.x, FBSIZE.y, true);
        g_pRenderer->pushFb(imageFB.m_iFb);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (border > 0)
            g_pRenderer->renderBorder(borderBox, color, border, BORDERROUND, 1.0);

        texbox.round();
        g_pRenderer->renderTexture(texbox, asset->texture, 1.0, ROUND, HYPRUTILS_TRANSFORM_NORMAL);
        g_pRenderer->popFb();
    }

    CTexture* tex    = &imageFB.m_cTex;
    CBox      texbox = {{}, tex->m_vSize};

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    pos = posFromHVAlign(viewport, tex->m_vSize, configPos, halign, valign, angle);

    texbox.x = pos.x;
    texbox.y = pos.y;

    texbox.round();
    texbox.rot = angle;
    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}

void CImage::renderUpdate() {
    auto newAsset = g_pAsyncResourceGatherer->getAssetByID(pendingResourceID);
    if (newAsset) {
        if (newAsset->texture.m_iType == TEXTURE_INVALID) {
            g_pAsyncResourceGatherer->unloadAsset(newAsset);
        } else if (resourceID != pendingResourceID) {
            g_pAsyncResourceGatherer->unloadAsset(asset);
            imageFB.destroyBuffer();

            asset       = newAsset;
            resourceID  = pendingResourceID;
            firstRender = true;
        }
        pendingResourceID = "";
    } else if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);
        pendingResourceID = "";
    } else if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);

        g_pHyprlock->addTimer(std::chrono::milliseconds(100), [REF = m_self](auto, auto) { onAssetCallback(REF); }, nullptr);
    }

    g_pHyprlock->renderOutput(stringPort);
}

CBox CImage::getBoundingBoxWl() const {
    if (!imageFB.isAllocated())
        return CBox{};

    return {
        Vector2D{pos.x, viewport.y - pos.y - imageFB.m_cTex.m_vSize.y},
        imageFB.m_cTex.m_vSize,
    };
}

void CImage::onClick(uint32_t button, bool down, const Vector2D& pos) {
    if (down && !onclickCommand.empty())
        spawnAsync(onclickCommand);
}

void CImage::onHover(const Vector2D& pos) {
    if (!onclickCommand.empty())
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
}
