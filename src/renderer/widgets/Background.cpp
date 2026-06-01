#include "Background.hpp"
#include "../Renderer.hpp"
#include "../AsyncResourceManager.hpp"
#include "../Framebuffer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../config/ConfigManager.hpp"
#include <chrono>
#include <hyprlang.hpp>
#include <filesystem>
#include <GLES3/gl32.h>
#include <array>
#include <algorithm>
#include <cctype>

#ifdef HYPRLOCK_WITH_MPV
#include "../MpvVideo.hpp"
#include <fstream>
#endif

static bool isVideoPath(const std::string& path) {
    static constexpr std::array<std::string_view, 11> VIDEO_EXTS = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v", ".flv", ".wmv", ".gif", ".ts", ".m2ts"};

    const auto DOT = path.find_last_of('.');
    if (DOT == std::string::npos)
        return false;

    std::string ext = path.substr(DOT);
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });

    return std::ranges::any_of(VIDEO_EXTS, [&](const auto& e) { return e == ext; });
}

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

static std::string runAndGetPath(const std::string& reloadCommand) {
    std::string path = spawnSync(reloadCommand);

    if (path.ends_with('\0'))
        path.pop_back();

    if (path.ends_with('\n'))
        path.pop_back();

    if (path.starts_with("file://"))
        path = path.substr(7);
    return path;
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
    scResourceID = CAsyncResourceManager::resourceIDForScreencopy(pOutput->stringPort);

    g_pAnimationManager->createAnimation(0.f, crossFadeProgress, g_pConfigManager->m_AnimationTree.getConfig("fadeIn"));

    if (!g_asyncResourceManager->checkIdPresent(scResourceID)) {
        Log::logger->log(Log::INFO, "Missing screenshot for output {}", outputPort);
        scResourceID = 0;
    }

    if (!reloadCommand.empty() && path.empty())
        path = runAndGetPath(reloadCommand);

    const bool PATH_IS_VIDEO = !isScreenshot && !path.empty() && isVideoPath(path);

    if (isScreenshot) {
        resourceID = scResourceID; // Fallback to solid background:color when scResourceID==0

        if (!g_pHyprlock->getScreencopy()) {
            Log::logger->log(Log::ERR, "No screencopy support! path=screenshot won't work. Falling back to background color.");
            resourceID = 0;
        }
    } else if (PATH_IS_VIDEO) {
#ifdef HYPRLOCK_WITH_MPV
        bool        videoLoop = true, videoMute = true;
        std::string videoHwdec = "auto-safe";
        try {
            videoLoop           = std::any_cast<Hyprlang::INT>(props.at("video_loop"));
            videoMute           = std::any_cast<Hyprlang::INT>(props.at("video_mute"));
            videoHwdec          = std::any_cast<Hyprlang::STRING>(props.at("video_hwdec"));
            videoFpsCap         = std::any_cast<Hyprlang::INT>(props.at("video_fps_cap"));
            videoPauseOnBattery = std::any_cast<Hyprlang::INT>(props.at("video_pause_on_battery"));
        } catch (const std::exception& e) { Log::logger->log(Log::ERR, "Failed to read video options for CBackground: {}", e.what()); }

        m_video = makeUnique<CMpvVideo>(absolutePath(path, ""), videoLoop, videoMute, videoHwdec);

        if (videoPauseOnBattery) {
            onBatteryTimerUpdate(); // set initial pause state
            plantBatteryTimer();
        }
#else
        Log::logger->log(Log::ERR, "background path '{}' looks like a video, but hyprlock was built without libmpv support. Falling back to background color.", path);
#endif
    } else if (!path.empty())
        resourceID = g_asyncResourceManager->requestImage(path, m_imageRevision, nullptr);

    if (!reloadCommand.empty() && reloadTime > -1 && !PATH_IS_VIDEO) {
        try {
            if (!isScreenshot)
                modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
        } catch (std::exception& e) { Log::logger->log(Log::ERR, "{}", e.what()); }

        plantReloadTimer(); // No reloads if reloadCommand is empty
    }
}

void CBackground::reset() {
    if (reloadTimer) {
        reloadTimer->cancel();
        reloadTimer.reset();
    }

#ifdef HYPRLOCK_WITH_MPV
    if (batteryTimer) {
        batteryTimer->cancel();
        batteryTimer.reset();
    }
    m_video.reset(); // frees mpv + its GL objects (binds the EGL context surfacelessly first)
    videoPaused      = false;
    m_lastVideoFrame = {};
#endif

    blurredFB->destroyBuffer();
    pendingBlurredFB->destroyBuffer();
}

void CBackground::updatePrimaryAsset() {
    if (asset || resourceID == 0)
        return;

    asset = g_asyncResourceManager->getAssetByID(resourceID);
    if (!asset)
        return;

    const bool NEEDFB = (isScreenshot || blurPasses > 0 || asset->m_vSize != viewport || transform != HYPRUTILS_TRANSFORM_NORMAL) && (!blurredFB->isAllocated() || firstRender);
    if (NEEDFB)
        renderToFB(*asset, *blurredFB, blurPasses, isScreenshot);
}

void CBackground::updatePendingAsset() {
    // For crossfading a new asset
    if (!pendingAsset || blurPasses == 0 || pendingBlurredFB->isAllocated())
        return;

    renderToFB(*pendingAsset, *pendingBlurredFB, blurPasses);
}

void CBackground::updateScAsset() {
    if (scAsset || scResourceID == 0)
        return;

    // path=screenshot -> scAsset = asset
    scAsset = (asset && isScreenshot) ? asset : g_asyncResourceManager->getAssetByID(scResourceID);
    if (!scAsset)
        return;

    const bool NEEDSCTRANSFORM = transform != HYPRUTILS_TRANSFORM_NORMAL;
    if (NEEDSCTRANSFORM)
        renderToFB(*scAsset, *transformedScFB, 0, true);
}

const CTexture& CBackground::getPrimaryAssetTex() const {
    // This case is only for background:path=screenshot with blurPasses=0
    if (isScreenshot && blurPasses == 0 && transformedScFB->isAllocated())
        return transformedScFB->m_cTex;

    return (blurredFB->isAllocated()) ? blurredFB->m_cTex : *asset;
}

const CTexture& CBackground::getPendingAssetTex() const {
    return (pendingBlurredFB->isAllocated()) ? pendingBlurredFB->m_cTex : *pendingAsset;
}

const CTexture& CBackground::getScAssetTex() const {
    return (transformedScFB->isAllocated()) ? transformedScFB->m_cTex : *scAsset;
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
#ifdef HYPRLOCK_WITH_MPV
    if (m_video)
        return drawVideo(data);
#endif

    updatePrimaryAsset();
    updatePendingAsset();
    updateScAsset();

    if (asset && asset->m_iType == TEXTURE_INVALID) {
        g_asyncResourceManager->unload(asset);
        resourceID = 0;
        renderRect(color);
        return false;
    }

    if (!asset || resourceID == 0) {
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
        return !asset && resourceID > 0; // resource not ready
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

void CBackground::onAssetUpdate(ResourceID id, ASP<CTexture> newAsset) {
    if (!newAsset)
        Log::logger->log(Log::ERR, "Background asset update failed, resourceID: {} not available on update!", id);
    else if (newAsset->m_iType == TEXTURE_INVALID) {
        g_asyncResourceManager->unload(newAsset);
        Log::logger->log(Log::ERR, "New background asset has an invalid texture!");
    } else {
        pendingAsset = newAsset;
        crossFadeProgress->setValueAndWarp(0);
        *crossFadeProgress = 1.0;

        crossFadeProgress->setCallbackOnEnd(
            [REF = m_self, id](auto) {
                if (const auto PSELF = REF.lock()) {
                    if (PSELF->asset)
                        g_asyncResourceManager->unload(PSELF->asset);

                    PSELF->asset        = PSELF->pendingAsset;
                    PSELF->pendingAsset = nullptr;
                    PSELF->resourceID   = id;

                    PSELF->blurredFB->destroyBuffer();
                    PSELF->blurredFB = std::move(PSELF->pendingBlurredFB);

                    PSELF->pendingResource = false;
                }
            },
            true);
    }
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
        path = runAndGetPath(reloadCommand);

        if (path.empty())
            return;
    }

    try {
        const auto MTIME = std::filesystem::last_write_time(absolutePath(path, ""));
        if (OLDPATH == path && MTIME == modificationTime)
            return;

        modificationTime = MTIME;
        if (OLDPATH == path)
            m_imageRevision++;
        else
            m_imageRevision = 0;
    } catch (std::exception& e) {
        path = OLDPATH;
        Log::logger->log(Log::ERR, "{}", e.what());
        return;
    }

    if (pendingResource) {
        Log::logger->log(Log::WARN, "Background image update still pending! - Refusing to load {}", path);
        return;
    }

    pendingResource = true;

    // Issue the next request
    AWP<IWidget> widget(m_self);
    g_asyncResourceManager->requestImage(path, m_imageRevision, widget);
}

#ifdef HYPRLOCK_WITH_MPV

bool CBackground::drawVideo(const SRenderData& data) {
    updateScAsset(); // enables the crossfade-from-screenshot during the lock fade-in

    // Render the solid color (and screenshot during fade-in) while there is no video frame to show yet.
    const auto renderFallback = [&]() {
        if (data.opacity < 1.0 && scAsset) {
            const auto& SCTEX  = getScAssetTex();
            const auto  SCTEXBOX = getScaledBoxForTextureSize(SCTEX.m_vSize, viewport);
            g_pRenderer->renderTexture(SCTEXBOX, SCTEX, 1, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);
            CHyprColor col = color;
            col.a *= data.opacity;
            renderRect(col);
            return true; // keep animating the fade
        }
        renderRect(color);
        return data.opacity < 1.0;
    };

    if (m_video->failed())
        return renderFallback();

    const CTexture* videoTex = nullptr;
    if (videoFpsCap > 0 && !videoPaused && m_video->lastFrame()) {
        // Steady-state playback: throttle how often we advance/redraw mpv to the configured cap.
        const auto NOW         = std::chrono::steady_clock::now();
        const auto MININTERVAL = std::chrono::nanoseconds((int64_t)(1'000'000'000.0 / videoFpsCap));
        if ((NOW - m_lastVideoFrame) >= MININTERVAL) {
            videoTex         = m_video->renderFrame(viewport);
            m_lastVideoFrame = NOW;
        } else
            videoTex = m_video->lastFrame();
    } else
        // Always render (even when paused) so mpv initializes and decodes its first frame; the mpv "pause"
        // property freezes advancement, so a paused video simply shows a static first/last frame.
        videoTex = m_video->renderFrame(viewport);

    if (!videoTex) {
        renderFallback();
        return !m_video->failed(); // keep the loop alive until the first frame decodes; idle once it gave up
    }

    // Reuse the image blur path: render the video frame into blurredFB and blur it in place.
    const CTexture* tex = videoTex;
    if (blurPasses > 0) {
        renderToFB(*videoTex, *blurredFB, blurPasses);
        tex = &blurredFB->m_cTex;
    }

    const auto TEXBOX = getScaledBoxForTextureSize(tex->m_vSize, viewport);
    if (data.opacity < 1.0 && scAsset) {
        const auto& SCTEX = getScAssetTex();
        g_pRenderer->renderTextureMix(TEXBOX, SCTEX, *tex, 1.0, data.opacity, 0);
    } else
        g_pRenderer->renderTexture(TEXBOX, *tex, 1, 0);

    // Animate while actively playing; idle the render loop when paused (battery) or stopped on the last frame.
    return !videoPaused && !m_video->ended();
}

static bool readFirstLine(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::getline(f, out);
    return true;
}

static bool onBatteryPower() {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path BASE = "/sys/class/power_supply";
    if (!fs::is_directory(BASE, ec))
        return false;

    bool sawMains = false, mainsOnline = false;
    bool sawBattery = false, batteryDischarging = false;

    for (const auto& entry : fs::directory_iterator(BASE, ec)) {
        std::string type;
        if (!readFirstLine(entry.path() / "type", type))
            continue;

        if (type == "Mains") {
            std::string online;
            if (readFirstLine(entry.path() / "online", online)) {
                sawMains = true;
                mainsOnline |= (online == "1");
            }
        } else if (type == "Battery") {
            std::string status;
            if (readFirstLine(entry.path() / "status", status)) {
                sawBattery = true;
                batteryDischarging |= (status == "Discharging");
            }
        }
    }

    if (sawMains)
        return !mainsOnline;
    if (sawBattery)
        return batteryDischarging;

    return false; // no power supply info (e.g. desktop) - never pause
}

static void onBatteryTimer(AWP<CBackground> ref) {
    if (auto PBG = ref.lock(); PBG) {
        PBG->onBatteryTimerUpdate();
        PBG->plantBatteryTimer();
    }
}

void CBackground::plantBatteryTimer() {
    batteryTimer = g_pHyprlock->addTimer(std::chrono::seconds(5), [REF = m_self](auto, auto) { onBatteryTimer(REF); }, nullptr);
}

void CBackground::onBatteryTimerUpdate() {
    if (!m_video)
        return;

    const bool ONBATTERY = onBatteryPower();
    if (ONBATTERY == videoPaused)
        return;

    videoPaused = ONBATTERY;
    m_video->setPaused(ONBATTERY);
    Log::logger->log(Log::INFO, "[mpv] video background {} (on battery: {})", ONBATTERY ? "paused" : "resumed", ONBATTERY);

    // While paused, drawVideo() returns false and the per-output frame loop idles - so a plain property
    // change won't be drawn. Kick a render so the pause/resume actually takes visible effect.
    g_pHyprlock->renderAllOutputs();
}

#endif // HYPRLOCK_WITH_MPV
