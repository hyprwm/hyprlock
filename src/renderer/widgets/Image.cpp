#include "Image.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include <cmath>

CImage::~CImage() {
    imageTimer->cancel();
    imageTimer.reset();
}

static void onTimer(std::shared_ptr<CTimer> self, void* data) {
    const auto PIMAGE = (CImage*)data;

    PIMAGE->onTimerUpdate();
    PIMAGE->plantTimer();
}

static void onAssetCallback(void* data) {
    const auto PIMAGE = (CImage*)data;
    PIMAGE->renderSuper();
}

void CImage::onTimerUpdate() {
    const std::string OLDPATH = path;

    if (!reloadCommand.empty()) {
        path = g_pHyprlock->spawnSync(reloadCommand);

        if (path.ends_with('\n'))
            path.pop_back();

        if (path.starts_with("file://"))
            path = path.substr(7);

        if (path.empty())
            return;
    }

    try {
        const auto MTIME = std::filesystem::last_write_time(path);
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

    request.callback     = onAssetCallback;
    request.callbackData = this;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CImage::plantTimer() {

    if (reloadTime == 0) {
        imageTimer = g_pHyprlock->addTimer(std::chrono::hours(1), onTimer, this, true);
    } else if (reloadTime > 0)
        imageTimer = g_pHyprlock->addTimer(std::chrono::seconds(reloadTime), onTimer, this, false);
}

CImage::CImage(const Vector2D& viewport_, COutput* output_, const std::string& resourceID_, const std::unordered_map<std::string, std::any>& props) :
    viewport(viewport_), resourceID(resourceID_), output(output_), shadow(this, props, viewport_) {

    size     = std::any_cast<Hyprlang::INT>(props.at("size"));
    rounding = std::any_cast<Hyprlang::INT>(props.at("rounding"));
    border   = std::any_cast<Hyprlang::INT>(props.at("border_size"));
    color    = std::any_cast<Hyprlang::INT>(props.at("border_color"));
    pos      = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    halign   = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign   = std::any_cast<Hyprlang::STRING>(props.at("valign"));
    angle    = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));

    path          = std::any_cast<Hyprlang::STRING>(props.at("path"));
    reloadTime    = std::any_cast<Hyprlang::INT>(props.at("reload_time"));
    reloadCommand = std::any_cast<Hyprlang::STRING>(props.at("reload_cmd"));

    try {
        modificationTime = std::filesystem::last_write_time(path);
    } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }

    angle = angle * M_PI / 180.0;

    plantTimer();
}

bool CImage::draw(const SRenderData& data) {

    if (!pendingResourceID.empty()) {
        auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(pendingResourceID);
        if (newAsset) {
            if (newAsset->texture.m_iType == TEXTURE_INVALID) {
                g_pRenderer->asyncResourceGatherer->unloadAsset(newAsset);
            } else if (resourceID != pendingResourceID) {
                g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
                imageFB.release();

                asset       = newAsset;
                resourceID  = pendingResourceID;
                firstRender = true;
            }
            pendingResourceID = "";
        }
    }

    if (resourceID.empty())
        return false;

    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return true;

    if (asset->texture.m_iType == TEXTURE_INVALID) {
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
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

        const bool ALLOWROUND = rounding > -1 && rounding < std::min(texbox.w, texbox.h) / 2.0;

        // plus borders if any
        CBox borderBox = {angle == 0 ? BORDERPOS : BORDERPOS + Vector2D{1.0, 1.0}, texbox.size() + IMAGEPOS * 2.0};

        borderBox.round();

        const Vector2D FBSIZE = angle == 0 ? borderBox.size() : borderBox.size() + Vector2D{2.0, 2.0};

        imageFB.alloc(FBSIZE.x, FBSIZE.y, true);
        g_pRenderer->pushFb(imageFB.m_iFb);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (border > 0)
            g_pRenderer->renderRect(borderBox, color, ALLOWROUND ? (rounding == 0 ? 0 : rounding + std::round(border / M_PI)) : std::min(borderBox.w, borderBox.h) / 2.0);

        texbox.round();
        g_pRenderer->renderTexture(texbox, asset->texture, 1.0, ALLOWROUND ? rounding : std::min(texbox.w, texbox.h) / 2.0, WL_OUTPUT_TRANSFORM_NORMAL);
        g_pRenderer->popFb();
    }

    CTexture* tex    = &imageFB.m_cTex;
    CBox      texbox = {{}, tex->m_vSize};

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    const auto TEXPOS = posFromHVAlign(viewport, tex->m_vSize, pos, halign, valign, angle);

    texbox.x = TEXPOS.x;
    texbox.y = TEXPOS.y;

    texbox.round();
    texbox.rot = angle;
    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, WL_OUTPUT_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}

static void onAssetCallbackTimer(std::shared_ptr<CTimer> self, void* data) {
    const auto PIMAGE = (CImage*)data;
    PIMAGE->renderSuper();
}

void CImage::renderSuper() {
    g_pHyprlock->renderOutput(output->stringPort);

    if (!pendingResourceID.empty()) /* did not consume the pending resource */
        g_pHyprlock->addTimer(std::chrono::milliseconds(100), onAssetCallbackTimer, this);
}
