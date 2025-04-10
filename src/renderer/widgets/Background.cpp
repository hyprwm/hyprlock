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

extern UP<CRenderer> g_pRenderer;

CBackground::~CBackground() {
    reset();
    if (m_bIsVideoBackground && !monitor.empty()) {
        g_pRenderer->stopMpvpaper(monitor);
        m_bIsVideoBackground = false;
    }
}

void CBackground::registerSelf(const SP<CBackground>& self) {
    m_self = self;
}

std::string CBackground::type() const {
    return "background";
}

bool CBackground::isVideoBackground() const {
    return m_bIsVideoBackground;
}

void CBackground::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    try {
        reset();

        // Parse properties
        if (props.contains("color")) {
            try {
                const auto& colorVal = props.at("color");
                if (colorVal.type() == typeid(Hyprlang::STRING)) {
                    std::string colorStr = std::any_cast<Hyprlang::STRING>(colorVal);
                    if (colorStr.starts_with("0x") || colorStr.starts_with("#"))
                        colorStr = colorStr.substr(2);
                    uint64_t colorValue = std::stoull(colorStr, nullptr, 16);
                    color = CHyprColor(colorValue);
                } else if (colorVal.type() == typeid(Hyprlang::INT)) {
                    uint64_t colorValue = std::any_cast<Hyprlang::INT>(colorVal);
                    color = CHyprColor(colorValue);
                } else {
                    throw std::bad_any_cast();
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse color: {}", e.what());
                color = CHyprColor(0, 0, 0, 0); // Transparent default for video backgrounds
            }
        } else {
            color = CHyprColor(0, 0, 0, 0);
        }

        blurPasses = 3;
        if (props.contains("blur_passes")) {
            try {
                const auto& val = props.at("blur_passes");
                if (val.type() == typeid(Hyprlang::INT)) {
                    blurPasses = std::any_cast<Hyprlang::INT>(val);
                } else {
                    Debug::log(WARN, "blur_passes has unexpected type, using default: 3");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse blur_passes: {}", e.what());
            }
        }

        blurSize = 10;
        if (props.contains("blur_size")) {
            try {
                const auto& val = props.at("blur_size");
                if (val.type() == typeid(Hyprlang::INT)) {
                    blurSize = std::any_cast<Hyprlang::INT>(val);
                } else {
                    Debug::log(WARN, "blur_size has unexpected type, using default: 10");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse blur_size: {}", e.what());
            }
        }

        vibrancy = 0.1696f;
        if (props.contains("vibrancy")) {
            try {
                const auto& val = props.at("vibrancy");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    vibrancy = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    vibrancy = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "vibrancy has unexpected type, using default: 0.1696");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse vibrancy: {}", e.what());
            }
        }

        vibrancy_darkness = 0.f;
        if (props.contains("vibrancy_darkness")) {
            try {
                const auto& val = props.at("vibrancy_darkness");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    vibrancy_darkness = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    vibrancy_darkness = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "vibrancy_darkness has unexpected type, using default: 0");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse vibrancy_darkness: {}", e.what());
            }
        }

        noise = 0.0117f;
        if (props.contains("noise")) {
            try {
                const auto& val = props.at("noise");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    noise = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    noise = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "noise has unexpected type, using default: 0.0117");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse noise: {}", e.what());
            }
        }

        brightness = 0.8172f;
        if (props.contains("brightness")) {
            try {
                const auto& val = props.at("brightness");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    brightness = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    brightness = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "brightness has unexpected type, using default: 0.8172");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse brightness: {}", e.what());
            }
        }

        contrast = 0.8916f;
        if (props.contains("contrast")) {
            try {
                const auto& val = props.at("contrast");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    contrast = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    contrast = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "contrast has unexpected type, using default: 0.8916");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse contrast: {}", e.what());
            }
        }

        path = "";
        if (props.contains("path")) {
            try {
                const auto& val = props.at("path");
                if (val.type() == typeid(Hyprlang::STRING)) {
                    path = std::any_cast<Hyprlang::STRING>(val);
                } else {
                    Debug::log(WARN, "path has unexpected type, using default: empty");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse path: {}", e.what());
            }
        }

        reloadCommand = "";
        if (props.contains("reload_cmd")) {
            try {
                const auto& val = props.at("reload_cmd");
                if (val.type() == typeid(Hyprlang::STRING)) {
                    reloadCommand = std::any_cast<Hyprlang::STRING>(val);
                } else {
                    Debug::log(WARN, "reload_cmd has unexpected type, using default: empty");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse reload_cmd: {}", e.what());
            }
        }

        reloadTime = -1;
        if (props.contains("reload_time")) {
            try {
                const auto& val = props.at("reload_time");
                if (val.type() == typeid(Hyprlang::INT)) {
                    reloadTime = std::any_cast<Hyprlang::INT>(val);
                } else {
                    Debug::log(WARN, "reload_time has unexpected type, using default: -1");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse reload_time: {}", e.what());
            }
        }

        crossFadeTime = -1.f;
        if (props.contains("crossfade_time")) {
            try {
                const auto& val = props.at("crossfade_time");
                if (val.type() == typeid(Hyprlang::FLOAT)) {
                    crossFadeTime = std::any_cast<Hyprlang::FLOAT>(val);
                } else if (val.type() == typeid(Hyprlang::INT)) {
                    crossFadeTime = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
                } else {
                    Debug::log(WARN, "crossfade_time has unexpected type, using default: -1");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse crossfade_time: {}", e.what());
            }
        }

        fallbackPath = "";
        if (props.contains("fallback_path")) {
            try {
                const auto& val = props.at("fallback_path");
                if (val.type() == typeid(Hyprlang::STRING)) {
                    fallbackPath = std::any_cast<Hyprlang::STRING>(val);
                } else {
                    Debug::log(WARN, "fallback_path has unexpected type, using default: empty");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse fallback_path: {}", e.what());
            }
        }

        isScreenshot = path == "screenshot";
        monitor = pOutput->stringPort;
        std::string type = "image";
        if (props.contains("type")) {
            try {
                const auto& val = props.at("type");
                if (val.type() == typeid(Hyprlang::STRING)) {
                    type = std::any_cast<Hyprlang::STRING>(val);
                } else {
                    Debug::log(WARN, "type has unexpected type, using default: image");
                }
            } catch (const std::exception& e) {
                Debug::log(ERR, "Failed to parse type: {}", e.what());
            }
        }

        m_bIsVideoBackground = false;
        videoPath = "";
        resourceID = "";
        if (type == "video" || path.ends_with(".mp4")) {
            videoPath = path;
            Debug::log(LOG, "Detected video background: {}", path);
            if (!path.empty()) {
                Debug::log(LOG, "Attempting to start mpvpaper for monitor {} with video {}", monitor, path);
                bool mpvSuccess = g_pRenderer->startMpvpaper(monitor, path);
                m_bIsVideoBackground = mpvSuccess;
                if (!mpvSuccess) {
                    if (!fallbackPath.empty() && !fallbackPath.ends_with(".mp4")) {
                        Debug::log(LOG, "Video background failed, using fallback: {}", fallbackPath);
                        resourceID = "background:" + fallbackPath;
                        m_bIsVideoBackground = false; // Ensure fallback image renders
                    } else {
                        Debug::log(ERR, "Video background failed and no valid fallback path provided, using transparent.");
                        resourceID = "";
                    }
                }
            }
        } else {
            resourceID = isScreenshot ? CScreencopyFrame::getResourceId(pOutput) : (!path.empty() && !path.ends_with(".mp4") ? "background:" + path : "");
        }

        viewport = pOutput->getViewport();
        outputPort = pOutput->stringPort;
        transform = isScreenshot ? wlTransformToHyprutils(invertTransform(pOutput->transform)) : HYPRUTILS_TRANSFORM_NORMAL;

        if (isScreenshot && !m_bIsVideoBackground) {
            if (g_pRenderer->asyncResourceGatherer->gathered) {
                if (!g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID))
                    resourceID = "";
            }
            if (!g_pHyprlock->getScreencopy()) {
                Debug::log(ERR, "No screencopy support! path=screenshot won't work. Falling back to transparent.");
                resourceID = "";
            }
        }

        if (!isScreenshot && !m_bIsVideoBackground && reloadTime > -1) {
            try {
                modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
            } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }
            plantReloadTimer();
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "Exception in CBackground::configure: {}", e.what());
        m_bIsVideoBackground = false;
        resourceID = "";
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
    CBox monbox = {0, 0, (int)viewport.x, (int)viewport.y};
    g_pRenderer->renderRect(monbox, color, 0);
}

bool CBackground::draw(const SRenderData& data) {
    if (m_bIsVideoBackground) {
        Debug::log(LOG, "Skipping static background rendering; using video background via mpvpaper");
        return false; // mpvpaper handles rendering
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

        Vector2D size = asset->texture.m_vSize;
        if (transform % 2 == 1 && isScreenshot) {
            size.x = asset->texture.m_vSize.y;
            size.y = asset->texture.m_vSize.x;
        }

        CBox texbox = {{}, size};
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
            blurredFB.alloc((int)viewport.x, (int)viewport.y);

        blurredFB.bind();

        if (fade)
            g_pRenderer->renderTextureMix(texbox, asset->texture, pendingAsset->texture, 1.0,
                                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - fade->start).count() / (1000 * crossFadeTime), 0,
                                          transform);
        else
            g_pRenderer->renderTexture(texbox, asset->texture, 1.0, 0, transform);

        if (blurPasses > 0)
            g_pRenderer->blurFB(blurredFB,
                                CRenderer::SBlurParams{.size = blurSize,
                                                       .passes = blurPasses,
                                                       .noise = noise,
                                                       .contrast = contrast,
                                                       .brightness = brightness,
                                                       .vibrancy = vibrancy,
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

    return fade || data.opacity < 1.0;
}

void CBackground::plantReloadTimer() {
    if (reloadTime == 0)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::hours(1),
            [REF = m_self](std::shared_ptr<CTimer>, void*) { REF.lock()->onReloadTimerUpdate(); }, nullptr, true);
    else if (reloadTime > -1)
        reloadTimer = g_pHyprlock->addTimer(std::chrono::seconds(reloadTime),
            [REF = m_self](std::shared_ptr<CTimer>, void*) { REF.lock()->onReloadTimerUpdate(); }, nullptr, true);
}

void CBackground::onReloadTimerUpdate() {
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

    if (m_bIsVideoBackground)
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

    request.id = std::string{"background:"} + path + ",time:" + std::to_string((uint64_t)modificationTime.time_since_epoch().count());
    pendingResourceID = request.id;
    request.asset = path;
    request.type = CAsyncResourceGatherer::eTargetType::TARGET_IMAGE;

    request.callback = [REF = m_self]() { REF.lock()->startCrossFadeOrUpdateRender(); };
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CBackground::onCrossFadeTimerUpdate() {
    if (fade) {
        fade->crossFadeTimer.reset();
        fade.reset();
    }

    if (blurPasses <= 0 && !isScreenshot)
        blurredFB.release();

    asset = pendingAsset;
    resourceID = pendingResourceID;
    pendingResourceID = "";
    pendingAsset = nullptr;
    firstRender = true;

    g_pHyprlock->renderOutput(outputPort);
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
                if (!fade)
                    fade = makeUnique<SFade>();
                else {
                    if (fade->crossFadeTimer) {
                        fade->crossFadeTimer->cancel();
                        fade->crossFadeTimer.reset();
                    }
                }
                fade->start = std::chrono::system_clock::now();
                fade->a = 0;
                fade->crossFadeTimer =
                    g_pHyprlock->addTimer(std::chrono::milliseconds((int)(1000.0 * crossFadeTime)),
                        [REF = m_self](std::shared_ptr<CTimer>, void*) { REF.lock()->onCrossFadeTimerUpdate(); }, nullptr, true);
            } else {
                onCrossFadeTimerUpdate();
            }
        }
    } else if (!pendingResourceID.empty()) {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", pendingResourceID);
        g_pHyprlock->addTimer(std::chrono::milliseconds(100),
            [REF = m_self](std::shared_ptr<CTimer>, void*) { REF.lock()->startCrossFadeOrUpdateRender(); }, nullptr, true);
    }

    g_pHyprlock->renderOutput(outputPort);
}