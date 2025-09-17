#include "Background.hpp"
#include "../Renderer.hpp"
#include "../Framebuffer.hpp"
#include "../Shared.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../config/ConfigManager.hpp"
#include "src/renderer/AsyncResourceGatherer.hpp"
#include <chrono>
#include <hyprlang.hpp>
#include <filesystem>
#include <GLES3/gl32.h>

CBackground::CBackground() {
    blurredFB        = makeUnique<CFramebuffer>();
    pendingBlurredFB = makeUnique<CFramebuffer>();
    transformedScFB  = makeUnique<CFramebuffer>();
}

CBackground::~CBackground() {
    reset();
}

void CBackground::registerSelf(const ASP<CBackground>& self) {
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
    transform    = wlTransformToHyprutils(invertTransform(pOutput->transform));
    scResourceID = CScreencopyFrame::getResourceId(pOutput);

    g_pAnimationManager->createAnimation(0.f, crossFadeProgress, g_pConfigManager->m_AnimationTree.getConfig("fadeIn"));

    // When the initial gather of the asyncResourceGatherer is completed (ready), all DMAFrames are available.
    // Dynamic ones are tricky, because a screencopy would copy hyprlock itself.
    if (g_pAsyncResourceGatherer->gathered && !g_pAsyncResourceGatherer->getAssetByID(scResourceID)) {
        Debug::log(LOG, "Missing screenshot for output {}", outputPort);
        scResourceID = "";
    }

    if (isScreenshot) {
        resourceID = scResourceID; // Fallback to solid background:color when scResourceID==""

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

    if (g_pAsyncResourceGatherer && scAsset)
        g_pAsyncResourceGatherer->unloadAsset(scAsset);

    blurredFB->destroyBuffer();
    pendingBlurredFB->destroyBuffer();
}

void CBackground::updatePrimaryAsset() {
    if (asset || resourceID.empty())
        return;

    asset = g_pAsyncResourceGatherer->getAssetByID(resourceID);
    if (!asset)
        return;

    const bool NEEDFB =
        (isScreenshot || blurPasses > 0 || asset->texture.m_vSize != viewport || transform != HYPRUTILS_TRANSFORM_NORMAL) && (!blurredFB->isAllocated() || firstRender);
    if (NEEDFB)
        renderToFB(asset->texture, *blurredFB, blurPasses, isScreenshot);
}

void CBackground::updatePendingAsset() {
    // For crossfading a new asset
    if (!pendingAsset || blurPasses == 0 || pendingBlurredFB->isAllocated())
        return;

    renderToFB(pendingAsset->texture, *pendingBlurredFB, blurPasses);
}

void CBackground::updateScAsset() {
    if (scAsset || scResourceID.empty())
        return;

    // path=screenshot -> scAsset = asset
    scAsset = (asset && isScreenshot) ? asset : g_pAsyncResourceGatherer->getAssetByID(scResourceID);
    if (!scAsset)
        return;

    const bool NEEDSCTRANSFORM = transform != HYPRUTILS_TRANSFORM_NORMAL;
    if (NEEDSCTRANSFORM)
        renderToFB(scAsset->texture, *transformedScFB, 0, true);
}

const CTexture& CBackground::getPrimaryAssetTex() const {
    // This case is only for background:path=screenshot with blurPasses=0
    if (isScreenshot && blurPasses == 0 && transformedScFB->isAllocated())
        return transformedScFB->m_cTex;

    return (blurredFB->isAllocated()) ? blurredFB->m_cTex : asset->texture;
}

const CTexture& CBackground::getPendingAssetTex() const {
    return (pendingBlurredFB->isAllocated()) ? pendingBlurredFB->m_cTex : pendingAsset->texture;
}

const CTexture& CBackground::getScAssetTex() const {
    return (transformedScFB->isAllocated()) ? transformedScFB->m_cTex : scAsset->texture;
}

void CBackground::renderRect(CHyprColor color) {
    CBox monbox = {0, 0, viewport.x, viewport.y};
    g_pRenderer->renderRect(monbox, color, 0);
}

static void onReloadTimer(AWP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG) {
        PBG->onReloadTimerUpdate();
        PBG->plantReloadTimer();
    }
}

static void onAssetCallback(AWP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG)
        PBG->startCrossFade();
}

static CBox getScaledBoxForTextureSize(const Vector2D& size, const Vector2D& viewport) {
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

    return texbox;
}

void CBackground::renderToFB(const CTexture& tex, CFramebuffer& fb, int passes, bool applyTransform) {
    if (firstRender)
        firstRender = false;

    // make it brah
    Vector2D size = tex.m_vSize;
    if (applyTransform && transform % 2 == 1) {
        size.x = tex.m_vSize.y;
        size.y = tex.m_vSize.x;
    }

    const auto TEXBOX = getScaledBoxForTextureSize(size, viewport);

    if (!fb.isAllocated())
        fb.alloc(viewport.x, viewport.y); // TODO 10 bit

    fb.bind();

    g_pRenderer->renderTexture(TEXBOX, tex, 1.0, 0, applyTransform ? transform : HYPRUTILS_TRANSFORM_NORMAL);

    if (blurPasses > 0)
        g_pRenderer->blurFB(fb,
                            CRenderer::SBlurParams{
                                .size              = blurSize,
                                .passes            = passes,
                                .noise             = noise,
                                .contrast          = contrast,
                                .brightness        = brightness,
                                .vibrancy          = vibrancy,
                                .vibrancy_darkness = vibrancy_darkness,
                            });
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

bool CBackground::draw(const SRenderData& data) {
    updatePrimaryAsset();
    updatePendingAsset();
    updateScAsset();

    if (asset && asset->texture.m_iType == TEXTURE_INVALID) {
        g_pAsyncResourceGatherer->unloadAsset(asset);
        resourceID = "";
        renderRect(color);
        return false;
    }

    if (!asset || resourceID.empty()) {
        // fade in/out with a solid color
        if (data.opacity < 1.0 && scAsset) {
            const auto& SCTEX    = getScAssetTex();
            const auto  SCTEXBOX = getScaledBoxForTextureSize(SCTEX.m_vSize, viewport);
            g_pRenderer->renderTexture(SCTEXBOX, SCTEX, 1, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);
            CHyprColor col = color;
            col.a *= data.opacity;
            renderRect(col);
            return true;
        }

        renderRect(color);
        return !asset && !resourceID.empty(); // resource not ready
    }

    const auto& TEX    = getPrimaryAssetTex();
    const auto  TEXBOX = getScaledBoxForTextureSize(TEX.m_vSize, viewport);
    if (data.opacity < 1.0 && scAsset) {
        const auto& SCTEX = getScAssetTex();
        g_pRenderer->renderTextureMix(TEXBOX, SCTEX, TEX, 1.0, data.opacity, 0);
    } else if (crossFadeProgress->isBeingAnimated()) {
        const auto& PENDINGTEX = getPendingAssetTex();
        g_pRenderer->renderTextureMix(TEXBOX, TEX, PENDINGTEX, 1.0, crossFadeProgress->value(), 0);
    } else
        g_pRenderer->renderTexture(TEXBOX, TEX, 1, 0);

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

    g_pAsyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CBackground::startCrossFade() {
    auto newAsset = g_pAsyncResourceGatherer->getAssetByID(pendingResourceID);
    if (newAsset) {
        if (newAsset->texture.m_iType == TEXTURE_INVALID) {
            g_pAsyncResourceGatherer->unloadAsset(newAsset);
            Debug::log(ERR, "New asset had an invalid texture!");
            pendingResourceID = "";
        } else if (resourceID != pendingResourceID) {
            pendingAsset = newAsset;
            crossFadeProgress->setValueAndWarp(0);
            *crossFadeProgress = 1.0;

            crossFadeProgress->setCallbackOnEnd(
                [REF = m_self](auto) {
                    if (const auto PSELF = REF.lock()) {
                        if (PSELF->asset)
                            g_pAsyncResourceGatherer->unloadAsset(PSELF->asset);
                        PSELF->asset             = PSELF->pendingAsset;
                        PSELF->pendingAsset      = nullptr;
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
