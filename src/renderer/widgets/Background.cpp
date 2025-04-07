#include "Background.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include <chrono>
#include <hyprlang.hpp>
#include <filesystem>
#include <memory>
#include <GLES3/gl32.h>

CBackground::~CBackground() {
    reset();
}

void CBackground::registerSelf(const SP<CBackground>& self) {
    m_self = self;
}

void CBackground::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    reset();

    try {
        color             = std::any_cast<Hyprlang::INT>(props.at("color"));
        blurPasses        = std::any_cast<Hyprlang::INT>(props.at("blur_passes"));
        blurSize          = std::any_cast<Hyprlang::INT>(props.at("blur_size"));
        vibrancy          = std::any_cast<Hyprlang::FLOAT>(props.at("vibrancy"));
        vibrancy_darkness = std::any_cast<Hyprlang::FLOAT>(props.at("vibrancy_darkness"));
        noise             = std::any_cast<Hyprlang::FLOAT>(props.at("noise"));
        brightness        = std::any_cast<Hyprlang::FLOAT>(props.at("brightness"));
        contrast          = std::any_cast<Hyprlang::FLOAT>(props.at("contrast"));
        path              = std::any_cast<Hyprlang::STRING>(props.at("path"));
        reloadCommand     = std::any_cast<Hyprlang::STRING>(props.at("reload_cmd"));
        reloadTime        = std::any_cast<Hyprlang::INT>(props.at("reload_time"));
        crossFadeTime     = std::any_cast<Hyprlang::FLOAT>(props.at("crossfade_time"));

    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CBackground: {}", e.what());
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing propperty for CBackground: {}", e.what());
    }

    isScreenshot = path == "screenshot";

    // Check if the path is an MP4 file
    if (path.ends_with(".mp4")) {
        isVideoBackground = true;
        videoPath = path;
        Debug::log(LOG, "Detected video background: {}", path);
        resourceID = "";  // Skip loading a static texture since we'll use mpvpaper
    } else {
        isVideoBackground = false;
        videoPath = "";
        resourceID = isScreenshot ? CScreencopyFrame::getResourceId(pOutput) : (!path.empty() ? "background:" + path : "");
    }

    viewport   = pOutput->getViewport();
    outputPort = pOutput->stringPort;
    transform  = isScreenshot ? wlTransformToHyprutils(invertTransform(pOutput->transform)) : HYPRUTILS_TRANSFORM_NORMAL;

    if (isScreenshot && !isVideoBackground) {
        // When the initial gather of the asyncResourceGatherer is completed (ready), all DMAFrames are available.
        // Dynamic ones are tricky, because a screencopy would copy hyprlock itself.
        if (g_pRenderer->asyncResourceGatherer->gathered) {
            if (!g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID))
                resourceID = ""; // Fallback to solid color (background:color)
        }

        if (!g_pHyprlock->getScreencopy()) {
            Debug::log(ERR, "No screencopy support! path=screenshot won't work. Falling back to background color.");
            resourceID = "";
        }
    }

    if (!isScreenshot && !isVideoBackground && reloadTime > -1) {
        try {
            modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
        } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }

        plantReloadTimer(); // No reloads for screenshots or videos
    }
}

void CBackground::reset() {
    if (reloadTimer) {
        reloadTimer->cancel();
        reloadTimer.reset();
    }

    if (fade) {
        if (fade->crossFadeTimer) {
            fade->crossFadeTimer->cancel();
            fade->crossFadeTimer.reset();
        }
        fade.reset();
    }
}

void CBackground::renderRect(CHyprColor color) {
    CBox monbox = {0, 0, viewport.x, viewport.y};
    g_pRenderer->renderRect(monbox, color, 0);
}

static void onReloadTimer(WP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG) {
        PBG->onReloadTimerUpdate();
        PBG->plantReloadTimer();
    }
}

static void onCrossFadeTimer(WP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG)
        PBG->onCrossFadeTimerUpdate();
}

static void onAssetCallback(WP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG)
        PBG->startCrossFadeOrUpdateRender();
}

bool CBackground::draw(const SRenderData& data) {
    if (isVideoBackground) {
        // Skip rendering the static background since mpvpaper is handling the video
        Debug::log(LOG, "Skipping static background rendering; using video background via mpvpaper");
        return false;
    }

    if (resourceID.empty()) {
        CHyprColor col = color;
        col.a *= data.opacity;
        renderRect(col);
        return data.opacity < 1.0;
    }

    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset) {
        CHyprColor col = color;
        col.a *= data.opacity;
        renderRect(col);
        return true;
    }

    if (asset->texture.m_iType == TEXTURE_INVALID) {
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
        resourceID = "";
        return true;
    }

    if (fade || ((blurPasses > 0 || isScreenshot) && (!blurredFB.isAllocated() || firstRender))) {
        if (firstRender)
            firstRender = false;

        // make it brah
        Vector2D size = asset->texture.m_vSize;
        if (transform % 2 == 1 && isScreenshot) {
            size.x = asset->texture.m_vSize.y;
            size.y = asset->texture.m_vSize.x;
        }

        CBox  texbox = {{}, size};

        float scaleX = viewport.x / size.x;
        float scaleY = viewport.y / size.y;

        texbox.w *= std::max(scaleX, scaleY);
        texbox.h *= std::max(scaleX, scaleY);

        if (scaleX > scaleY)
            texbox.y = -(texbox.h - viewport.y) / 2.f;
        else
            texbox.x = -(texbox.w - viewport.x) / 2.f;
        texbox.round();

        if (!blurredFB.isAllocated())
            blurredFB.alloc(viewport.x, viewport.y); // TODO 10 bit

        blurredFB.bind();

        if (fade)
            g_pRenderer->renderTextureMix(texbox, asset->texture, pendingAsset->texture, 1.0,
                                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - fade->start).count() / (1000 * crossFadeTime), 0,
                                          transform);
        else
            g_pRenderer->renderTexture(texbox, asset->texture, 1.0, 0, transform);

        if (blurPasses > 0)
            g_pRenderer->blurFB(blurredFB,
                                CRenderer::SBlurParams{.size              = blurSize,
                                                       .passes            = blurPasses,
                                                       .noise             = noise,
                                                       .contrast          = contrast,
                                                       .brightness        = brightness,
                                                       .vibrancy          = vibrancy,
                                                       .vibrancy_darkness = vibrancy_darkness});
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    CTexture* tex = blurredFB.isAllocated() ? &blurredFB.m_cTex : &asset->texture;

    CBox      texbox = {{}, tex->m_vSize};

    Vector2D  size   = tex->m_vSize;
    float     scaleX = viewport.x / tex->m_vSize.x;
    float     scaleY = viewport.y / tex->m_vSize.y;

    texbox.w *= std::max(scaleX, scaleY);
    texbox.h *= std::max(scaleX, scaleY);

    if (scaleX > scaleY)
        texbox.y = -(texbox.h - viewport.y) / 2.f;
    else
        texbox.x = -(texbox.w - viewport.x) / 2.f;
    texbox.round();
    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);

    return fade || data.opacity < 1.0; // actively render during fading
}
void CBackground::plantReloadTimer() {
    if (reloadTime == 0)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::hours(1), [REF = m_self](auto, auto) { onReloadTimer(REF); }, nullptr, true);
    else if (reloadTime > 0)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::seconds(reloadTime), [REF = m_self](auto, auto) { onReloadTimer(REF); }, nullptr, true);
}

void CBackground::onCrossFadeTimerUpdate() {
    // Animation done: Unload previous asset, deinitialize the fade and pass the asset
    if (fade) {
        fade->crossFadeTimer.reset();
        fade.reset();
    }

    if (blurPasses <= 0 && !isScreenshot)
        blurredFB.release();

    asset             = pendingAsset;
    resourceID        = pendingResourceID;
    pendingResourceID = "";
    pendingAsset      = nullptr;
    firstRender       = true;

    g_pHyprlock->renderOutput(outputPort);
}

void CBackground::onReloadTimerUpdate() {
    const std::string OLDPATH = path;

    // Path parsing and early returns
    if (!reloadCommand.empty()) {
        path = g_pHyprlock->spawnSync(reloadCommand);

        if (path.ends_with('\n'))
            path.pop_back();

        if (path.starts_with("file://"))
            path = path.substr(7);

        if (path.empty())
            return;
    }

    // Skip reload for video backgrounds
    if (isVideoBackground)
        return;

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

    // Issue the next request
    request.id        = std::string{"background:"} + path + ",time:" + std::to_string((uint64_t)modificationTime.time_since_epoch().count());
    pendingResourceID = request.id;
    request.asset     = path;
    request.type      = CAsyncResourceGatherer::eTargetType::TARGET_IMAGE;

    request.callback = [REF = m_self]() { onAssetCallback(REF); };

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CBackground::startCrossFadeOrUpdateRender() {
    auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(pendingResourceID);
    if (newAsset) {
        if (newAsset->texture.m_iType == TEXTURE_INVALID) {
            g_pRenderer->asyncResourceGatherer->unloadAsset(newAsset);
            Debug::log(ERR, "New asset had an invalid texture!");
        } else if (resourceID != pendingResourceID) {
            pendingAsset = newAsset;
            if (crossFadeTime > 0) {
                // Start a fade
                if (!fade)
                    fade = makeUnique<SFade>(std::chrono::system_clock::now(), 0, nullptr);
                else {
                    // Maybe we were already fading so reset it just in case, but shouldn't be happening.
                    if (fade->crossFadeTimer) {
                        fade->crossFadeTimer->cancel();
                        fade->crossFadeTimer.reset();
                    }
                }
                fade->start = std::chrono::system_clock::now();
                fade->a     = 0;
                fade->crossFadeTimer =
                    g_pHyprlock->addTimer(std::chrono::milliseconds((int)(1000.0 * crossFadeTime)), [REF = m_self](auto, auto) { onCrossFadeTimer(REF); }, nullptr);
            } else {
                onCrossFadeTimerUpdate();
            }
        }
    } else if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);
        g_pHyprlock->addTimer(std::chrono::milliseconds(100), [REF = m_self](auto, auto) { onAssetCallback(REF); }, nullptr);
    }

    g_pHyprlock->renderOutput(outputPort);
}