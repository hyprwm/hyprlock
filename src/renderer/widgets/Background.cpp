#include "Background.hpp"
#include "../Renderer.hpp"
#include "../Framebuffer.hpp"
#include "../Shared.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../config/ConfigManager.hpp"
#include <chrono>
#include <hyprlang.hpp>
#include <filesystem>
#include <memory>
#include <GLES3/gl32.h>

CBackground::CBackground() {
    blurredFB        = makeUnique<CFramebuffer>();
    pendingBlurredFB = makeUnique<CFramebuffer>();
}

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

    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CBackground: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing propperty for CBackground: {}", e.what()); //
    }

    isScreenshot = path == "screenshot";

    viewport     = pOutput->getViewport();
    outputPort   = pOutput->stringPort;
    transform    = isScreenshot ? wlTransformToHyprutils(invertTransform(pOutput->transform)) : HYPRUTILS_TRANSFORM_NORMAL;
    scResourceID = CScreencopyFrame::getResourceId(pOutput);

    g_pAnimationManager->createAnimation(0.f, crossFadeProgress, g_pConfigManager->m_AnimationTree.getConfig("fadeIn"));

    if (isScreenshot) {
        resourceID = scResourceID;
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

    } else if (!path.empty())
        resourceID = "background:" + path;

    if (!isScreenshot && reloadTime > -1) {
        try {
            modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
        } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }

        plantReloadTimer(); // No reloads for screenshots.
    }
}

void CBackground::reset() {
    if (reloadTimer) {
        reloadTimer->cancel();
        reloadTimer.reset();
    }

    blurredFB->destroyBuffer();
    pendingBlurredFB->destroyBuffer();
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

static void onAssetCallback(WP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG)
        PBG->startCrossFade();
}

void CBackground::renderBlur(const CTexture& tex, CFramebuffer& fb) {
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

    if (!fb.isAllocated())
        fb.alloc(viewport.x, viewport.y); // TODO 10 bit

    fb.bind();

    g_pRenderer->renderTexture(texbox, tex, 1.0, 0, transform);

    if (blurPasses > 0)
        g_pRenderer->blurFB(fb,
                            CRenderer::SBlurParams{.size              = blurSize,
                                                   .passes            = blurPasses,
                                                   .noise             = noise,
                                                   .contrast          = contrast,
                                                   .brightness        = brightness,
                                                   .vibrancy          = vibrancy,
                                                   .vibrancy_darkness = vibrancy_darkness});
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

static CBox getScaledBoxForTexture(const CTexture& tex, const Vector2D& viewport) {
    CBox     texbox = {{}, tex.m_vSize};

    Vector2D size   = tex.m_vSize;
    float    scaleX = viewport.x / tex.m_vSize.x;
    float    scaleY = viewport.y / tex.m_vSize.y;

    texbox.w *= std::max(scaleX, scaleY);
    texbox.h *= std::max(scaleX, scaleY);

    if (scaleX > scaleY)
        texbox.y = -(texbox.h - viewport.y) / 2.f;
    else
        texbox.x = -(texbox.w - viewport.x) / 2.f;
    texbox.round();

    return texbox;
}

bool CBackground::draw(const SRenderData& data) {
    if (!asset && !resourceID.empty())
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    // path=screenshot -> scAsset = asset
    if (!scAsset)
        scAsset = (asset && isScreenshot) ? asset : g_pRenderer->asyncResourceGatherer->getAssetByID(scResourceID);

    if (!asset || resourceID.empty()) {
        // fade in/out with a solid color
        if (data.opacity < 1.0 && scAsset) {
            const auto SCTEXBOX = getScaledBoxForTexture(scAsset->texture, viewport);
            g_pRenderer->renderTexture(SCTEXBOX, scAsset->texture, 1, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);
            CHyprColor col = color;
            col.a *= data.opacity;
            renderRect(col);
            return true;
        }

        renderRect(color);
        return !asset && !resourceID.empty(); // resource not ready
    }

    if (asset->texture.m_iType == TEXTURE_INVALID) {
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
        resourceID = "";
        renderRect(color);
        return false;
    }

    if (asset && (blurPasses > 0 || isScreenshot) && (!blurredFB->isAllocated() || firstRender))
        renderBlur(asset->texture, *blurredFB);

    // For crossfading a new asset
    if (pendingAsset && blurPasses > 0 && !pendingBlurredFB->isAllocated())
        renderBlur(pendingAsset->texture, *pendingBlurredFB);

    const auto& TEX    = blurredFB->isAllocated() ? blurredFB->m_cTex : asset->texture;
    const auto  TEXBOX = getScaledBoxForTexture(TEX, viewport);

    if (data.opacity < 1.0 && scAsset)
        g_pRenderer->renderTextureMix(TEXBOX, scAsset->texture, TEX, 1.0, data.opacity, 0);
    else if (crossFadeProgress->isBeingAnimated()) {
        const auto& PENDINGTEX = pendingBlurredFB->isAllocated() ? pendingBlurredFB->m_cTex : pendingAsset->texture;
        g_pRenderer->renderTextureMix(TEXBOX, TEX, PENDINGTEX, 1.0, crossFadeProgress->value(), 0);
    } else
        g_pRenderer->renderTexture(TEXBOX, TEX, 1, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);

    return crossFadeProgress->isBeingAnimated() || data.opacity < 1.0;
}

void CBackground::plantReloadTimer() {

    if (reloadTime == 0)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::hours(1), [REF = m_self](auto, auto) { onReloadTimer(REF); }, nullptr, true);
    else if (reloadTime > 0)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::seconds(reloadTime), [REF = m_self](auto, auto) { onReloadTimer(REF); }, nullptr, true);
}

void CBackground::onReloadTimerUpdate() {
    const std::string OLDPATH = path;

    // Path parsing and early returns

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

    // Issue the next request

    request.id        = std::string{"background:"} + path + ",time:" + std::to_string((uint64_t)modificationTime.time_since_epoch().count());
    pendingResourceID = request.id;
    request.asset     = path;
    request.type      = CAsyncResourceGatherer::eTargetType::TARGET_IMAGE;

    request.callback = [REF = m_self]() { onAssetCallback(REF); };

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CBackground::startCrossFade() {
    auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(pendingResourceID);
    if (newAsset) {
        if (newAsset->texture.m_iType == TEXTURE_INVALID) {
            g_pRenderer->asyncResourceGatherer->unloadAsset(newAsset);
            Debug::log(ERR, "New asset had an invalid texture!");
            pendingResourceID = "";
        } else if (resourceID != pendingResourceID) {
            pendingAsset = newAsset;
            crossFadeProgress->setValueAndWarp(0);
            *crossFadeProgress = 1.0;

            crossFadeProgress->setCallbackOnEnd(
                [REF = m_self](auto) {
                    if (const auto PSELF = REF.lock()) {
                        PSELF->asset        = PSELF->pendingAsset;
                        PSELF->pendingAsset = nullptr;
                        g_pRenderer->asyncResourceGatherer->unloadAsset(PSELF->pendingAsset);
                        PSELF->resourceID        = PSELF->pendingResourceID;
                        PSELF->pendingResourceID = "";

                        PSELF->blurredFB->destroyBuffer();
                        PSELF->blurredFB = std::move(PSELF->pendingBlurredFB);
                    }
                },
                true);

            g_pHyprlock->renderOutput(outputPort);
        }
    } else if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);
        g_pHyprlock->addTimer(std::chrono::milliseconds(100), [REF = m_self](auto, auto) { onAssetCallback(REF); }, nullptr);
    }
}
