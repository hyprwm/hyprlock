#include "Background.hpp"
#include "../Renderer.hpp"
#include "../AsyncResourceManager.hpp"
#include "../Framebuffer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../config/ConfigManager.hpp"
#include <algorithm>
#include <chrono>
#include <hyprlang.hpp>
#include <filesystem>
#include <unordered_set>
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

// ── Video support ─────────────────────────────────────────────────────────────

SVideoState::~SVideoState() {
    running = false;
    if (decodeThread.joinable())
        decodeThread.join();
    if (swsCtx)    sws_freeContext(swsCtx);
    if (codecCtx)  avcodec_free_context(&codecCtx);
    if (formatCtx) avformat_close_input(&formatCtx);
}

bool CBackground::isVideoFile(const std::string& path) {
    static const std::unordered_set<std::string> VIDEO_EXT = {
        ".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v",
        ".flv", ".wmv", ".ts",  ".m2ts", ".gif"
    };
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return VIDEO_EXT.count(ext) > 0;
}

bool CBackground::openVideo(const std::string& path) {
    auto& v = *m_video;

    if (avformat_open_input(&v.formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        Debug::log(ERR, "CBackground: avformat_open_input failed for {}", path);
        return false;
    }
    if (avformat_find_stream_info(v.formatCtx, nullptr) < 0) {
        Debug::log(ERR, "CBackground: avformat_find_stream_info failed for {}", path);
        return false;
    }

    const AVCodec* codec = nullptr;
    v.streamIdx = av_find_best_stream(v.formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (v.streamIdx < 0 || !codec) {
        Debug::log(ERR, "CBackground: no video stream found in {}", path);
        return false;
    }

    v.codecCtx = avcodec_alloc_context3(codec);
    if (!v.codecCtx) {
        Debug::log(ERR, "CBackground: avcodec_alloc_context3 failed");
        return false;
    }

    avcodec_parameters_to_context(v.codecCtx, v.formatCtx->streams[v.streamIdx]->codecpar);

    if (avcodec_open2(v.codecCtx, codec, nullptr) < 0) {
        Debug::log(ERR, "CBackground: avcodec_open2 failed for {}", path);
        return false;
    }

    v.frameW   = v.codecCtx->width;
    v.frameH   = v.codecCtx->height;
    v.timeBase = av_q2d(v.formatCtx->streams[v.streamIdx]->time_base);

    // swsCtx is created lazily per-frame via sws_getCachedContext so that
    // we handle codecs where pix_fmt is only known after the first decode.
    v.frameData.resize(4 * v.frameW * v.frameH);
    Debug::log(LOG, "CBackground: opened video {} ({}x{}, timebase={:.6f})",
               path, v.frameW, v.frameH, v.timeBase);
    return true;
}

void CBackground::startVideoThread() {
    auto& v    = *m_video;
    v.running  = true;
    v.startTime = std::chrono::steady_clock::now();

    v.decodeThread = std::thread([&v]() {
        AVPacket* pkt   = av_packet_alloc();
        AVFrame*  frame = av_frame_alloc();
        std::vector<uint8_t> tmpBuf(4 * v.frameW * v.frameH);

        while (v.running) {
            int ret = av_read_frame(v.formatCtx, pkt);

            if (ret == AVERROR_EOF) {
                // Flush decoder's internal buffer
                avcodec_send_packet(v.codecCtx, nullptr);
                while (avcodec_receive_frame(v.codecCtx, frame) == 0)
                    av_frame_unref(frame);

                // Loop: seek back to beginning
                av_seek_frame(v.formatCtx, v.streamIdx, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(v.codecCtx);
                v.startTime = std::chrono::steady_clock::now();
                continue;
            }

            if (ret < 0)
                break; // unrecoverable error

            if (pkt->stream_index != v.streamIdx) {
                av_packet_unref(pkt);
                continue;
            }

            if (avcodec_send_packet(v.codecCtx, pkt) < 0) {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_unref(pkt);

            while (avcodec_receive_frame(v.codecCtx, frame) == 0) {
                if (!v.running)
                    break;

                // Lazily create / update SwsContext to match the frame's actual
                // pixel format (some codecs only report it after the first frame).
                v.swsCtx = sws_getCachedContext(v.swsCtx,
                    frame->width, frame->height, (AVPixelFormat)frame->format,
                    v.frameW, v.frameH, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!v.swsCtx) {
                    av_frame_unref(frame);
                    continue;
                }

                // sws_scale requires 4-element pointer/stride arrays even for
                // packed formats — passing a 1-element array causes UB reads.
                uint8_t* dst[4]    = {tmpBuf.data(), nullptr, nullptr, nullptr};
                int      stride[4] = {4 * v.frameW,  0,       0,       0};
                sws_scale(v.swsCtx,
                          (const uint8_t* const*)frame->data, frame->linesize,
                          0, frame->height, dst, stride);

                // Publish frame via O(1) swap (no memcpy)
                {
                    std::lock_guard<std::mutex> lock(v.frameMutex);
                    std::swap(v.frameData, tmpBuf);
                    v.hasNewFrame = true;
                }
                // tmpBuf now holds old frame data and will be overwritten next iteration

                // PTS-based frame pacing
                if (frame->pts != AV_NOPTS_VALUE) {
                    double pts_sec = frame->pts * v.timeBase;
                    auto   target  = v.startTime +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(pts_sec));
                    auto now       = std::chrono::steady_clock::now();
                    // Safety cap: never sleep > 5 s (guards against bogus PTS values)
                    auto maxTarget = now + std::chrono::seconds(5);
                    if (target > now && target < maxTarget)
                        std::this_thread::sleep_until(target);
                }

                av_frame_unref(frame);
            }
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);
    });
}

void CBackground::stopVideo() {
    m_video.reset(); // ~SVideoState(): sets running=false, joins thread, frees ffmpeg
    m_videoTexture.destroyTexture();
    m_uploadBuffer.clear();
    m_isVideo = false;
}

// ── End video support ─────────────────────────────────────────────────────────

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
        Debug::log(LOG, "Missing screenshot for output {}", outputPort);
        scResourceID = 0;
    }

    if (!reloadCommand.empty() && path.empty())
        path = runAndGetPath(reloadCommand);

    if (isScreenshot) {
        resourceID = scResourceID; // Fallback to solid background:color when scResourceID==0

        if (!g_pHyprlock->getScreencopy()) {
            Debug::log(ERR, "No screencopy support! path=screenshot won't work. Falling back to background color.");
            resourceID = 0;
        }
    } else if (!path.empty()) {
        if (isVideoFile(path)) {
            m_isVideo = true;
            m_video   = makeUnique<SVideoState>();
            if (!openVideo(path)) {
                Debug::log(ERR, "CBackground: failed to open '{}' as video, falling back to image", path);
                m_video.reset();
                m_isVideo = false;
                resourceID = g_asyncResourceManager->requestImage(path, m_imageRevision, nullptr);
            } else {
                // Pre-size the upload buffer so it's never empty when the main
                // thread swaps it with frameData.  An empty vector has data()==null
                // which would propagate back to tmpBuf in the decode thread and
                // cause "bad dst image pointers" on the third sws_scale call.
                m_uploadBuffer.resize(4 * m_video->frameW * m_video->frameH);
                startVideoThread();
            }
        } else {
            resourceID = g_asyncResourceManager->requestImage(path, m_imageRevision, nullptr);
        }
    }

    if (!reloadCommand.empty() && reloadTime > -1 && !m_isVideo) {
        try {
            if (!isScreenshot)
                modificationTime = std::filesystem::last_write_time(absolutePath(path, ""));
        } catch (std::exception& e) { Debug::log(ERR, "{}", e.what()); }

        plantReloadTimer(); // No reloads if reloadCommand is empty
    }
}

void CBackground::reset() {
    if (m_isVideo)
        stopVideo();

    if (reloadTimer) {
        reloadTimer->cancel();
        reloadTimer.reset();
    }

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
    // ── Video background fast path ────────────────────────────────────────
    if (m_isVideo && m_video) {
        // Grab the latest decoded frame from the decode thread (O(1) swap)
        {
            std::lock_guard<std::mutex> lock(m_video->frameMutex);
            if (m_video->hasNewFrame) {
                std::swap(m_uploadBuffer, m_video->frameData);
                m_video->hasNewFrame = false;
            }
        }

        // Upload frame to GL texture
        if (!m_uploadBuffer.empty()) {
            if (!m_videoTexture.m_bAllocated) {
                // First frame: allocate the GL texture
                m_videoTexture.allocate();
                glBindTexture(GL_TEXTURE_2D, m_videoTexture.m_iTexID);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                             m_video->frameW, m_video->frameH, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                m_videoTexture.m_vSize   = {(double)m_video->frameW, (double)m_video->frameH};
                m_videoTexture.m_iType   = TEXTURE_RGBA;
                m_videoTexture.m_iTarget = GL_TEXTURE_2D;
            } else {
                glBindTexture(GL_TEXTURE_2D, m_videoTexture.m_iTexID);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                m_video->frameW, m_video->frameH,
                                GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Re-render blur FB if requested (GPU-side only, called every new frame)
            if (blurPasses > 0)
                renderToFB(m_videoTexture, *blurredFB, blurPasses);
        }

        // Render
        if (!m_videoTexture.m_bAllocated) {
            renderRect(color); // solid colour until first frame is ready
            return true;
        }

        const CTexture& TEX    = (blurPasses > 0 && blurredFB->isAllocated())
                                     ? blurredFB->m_cTex : m_videoTexture;
        const auto      TEXBOX = getScaledBoxForTextureSize(TEX.m_vSize, viewport);
        g_pRenderer->renderTexture(TEXBOX, TEX, data.opacity);
        return true; // always request the next compositor frame
    }
    // ── End video path ────────────────────────────────────────────────────

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
    pendingResource = false;

    if (!newAsset)
        Debug::log(ERR, "Background asset update failed, resourceID: {} not available on update!", id);
    else if (newAsset->m_iType == TEXTURE_INVALID) {
        g_asyncResourceManager->unload(newAsset);
        Debug::log(ERR, "New background asset has an invalid texture!");
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
        Debug::log(ERR, "{}", e.what());
        return;
    }

    if (pendingResource)
        return;

    pendingResource = true;

    // Issue the next request
    AWP<IWidget> widget(m_self);
    g_asyncResourceManager->requestImage(path, m_imageRevision, widget);
}
