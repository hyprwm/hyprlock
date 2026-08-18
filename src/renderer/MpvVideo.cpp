#include "MpvVideo.hpp"

#ifdef HYPRLOCK_WITH_MPV

#include "../core/Egl.hpp"
#include "../helpers/Log.hpp"

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <chrono>

// No first frame within this long after init => assume a dead/trackless file and stop spinning.
static constexpr auto MPV_FIRST_FRAME_TIMEOUT = std::chrono::seconds(10);

static void* getProcAddressMpv(void* ctx, const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

CMpvVideo::CMpvVideo(const std::string& path, bool loop, bool mute, const std::string& hwdec) :
    m_path(path), m_hwdec(hwdec.empty() ? "auto-safe" : hwdec), m_loop(loop), m_mute(mute) {
    ;
}

CMpvVideo::~CMpvVideo() {
    cleanup();
}

void CMpvVideo::onMpvRenderUpdate(void* ctx) {
    // Foreign thread - must not call any mpv API. Just flag that a frame may be ready.
    static_cast<CMpvVideo*>(ctx)->m_updateRequested.store(true, std::memory_order_relaxed);
}

bool CMpvVideo::ensureInitialized() {
    if (m_initialized)
        return !m_failed;

    m_initialized = true;

    m_mpv = mpv_create();
    if (!m_mpv) {
        Log::logger->log(Log::ERR, "[mpv] mpv_create() failed - video background unavailable");
        m_failed = true;
        return false;
    }

    // Options must be set before mpv_initialize.
    mpv_set_option_string(m_mpv, "vo", "libmpv"); // required for the render API
    if (mpv_set_option_string(m_mpv, "hwdec", m_hwdec.c_str()) < 0)
        Log::logger->log(Log::WARN, "[mpv] hwdec '{}' was rejected; falling back to software decoding", m_hwdec);
    mpv_set_option_string(m_mpv, "loop-file", m_loop ? "inf" : "no");
    mpv_set_option_string(m_mpv, "mute", m_mute ? "yes" : "no");
    if (m_mute)
        mpv_set_option_string(m_mpv, "audio", "no"); // don't even open an audio output

    // Wallpaper-style cover-fit: fill the surface, cropping overflow instead of letterboxing.
    mpv_set_option_string(m_mpv, "panscan", "1.0");
    mpv_set_option_string(m_mpv, "keepaspect", "yes");

    // Behave as an unattended, sandboxed player: no user config, no scripts, no input, no terminal.
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "load-scripts", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");

    if (mpv_initialize(m_mpv) < 0) {
        Log::logger->log(Log::ERR, "[mpv] mpv_initialize() failed - video background unavailable");
        m_failed = true;
        return false;
    }

    mpv_request_log_messages(m_mpv, "error");

    mpv_opengl_init_params glInit{};
    glInit.get_proc_address     = &getProcAddressMpv;
    glInit.get_proc_address_ctx = nullptr;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&m_glCtx, m_mpv, params) < 0) {
        Log::logger->log(Log::ERR, "[mpv] mpv_render_context_create() failed - video background unavailable");
        m_failed = true;
        return false;
    }

    mpv_render_context_set_update_callback(m_glCtx, &onMpvRenderUpdate, this);

    const char* cmd[] = {"loadfile", m_path.c_str(), nullptr};
    if (mpv_command(m_mpv, cmd) < 0) {
        Log::logger->log(Log::ERR, "[mpv] failed to load video file: {} - falling back to background color", m_path);
        m_failed = true;
        return false;
    }

    if (m_paused)
        mpv_set_property_string(m_mpv, "pause", "yes");

    m_initTime = std::chrono::steady_clock::now();
    Log::logger->log(Log::INFO, "[mpv] video background initialized (hwdec={}, loop={}): {}", m_hwdec, m_loop, m_path);
    return true;
}

void CMpvVideo::pumpEvents() {
    if (!m_mpv)
        return;

    while (true) {
        mpv_event* ev = mpv_wait_event(m_mpv, 0); // non-blocking
        if (ev->event_id == MPV_EVENT_NONE)
            break;

        switch (ev->event_id) {
            case MPV_EVENT_LOG_MESSAGE: {
                const auto* msg = static_cast<mpv_event_log_message*>(ev->data);
                Log::logger->log(Log::ERR, "[mpv] {}", msg->text); // text already ends with '\n'
                break;
            }
            case MPV_EVENT_END_FILE: {
                const auto* ef = static_cast<mpv_event_end_file*>(ev->data);
                if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                    Log::logger->log(Log::ERR, "[mpv] playback error for {}: {} - falling back to background color", m_path, mpv_error_string(ef->error));
                    m_failed = true;
                    return;
                }
                if (ef->reason == MPV_END_FILE_REASON_EOF) {
                    // Only happens with video_loop=false: freeze on the last frame and let the loop idle.
                    Log::logger->log(Log::INFO, "[mpv] reached end of {} (not looping) - holding last frame", m_path);
                    m_ended = true;
                }
                break;
            }
            case MPV_EVENT_SHUTDOWN:
                Log::logger->log(Log::ERR, "[mpv] received shutdown event - stopping video background");
                m_failed = true;
                return;
            default: break;
        }
    }
}

const CTexture* CMpvVideo::renderFrame(const Vector2D& size) {
    if (m_failed || !ensureInitialized())
        return nullptr;

    pumpEvents();
    if (m_failed)
        return nullptr;

    const int W = std::max(1, (int)size.x);
    const int H = std::max(1, (int)size.y);

    const bool RESIZED = !m_fb.isAllocated() || m_fb.m_vSize != Vector2D(W, H);
    if (RESIZED)
        m_fb.alloc(W, H);

    const bool HASNEW = (mpv_render_context_update(m_glCtx) & MPV_RENDER_UPDATE_FRAME) || m_updateRequested.exchange(false, std::memory_order_relaxed);

    if (HASNEW || (RESIZED && m_hasFrame)) {
        // mpv freely mutates GL state - snapshot what hyprlock's renderer relies on and restore it after.
        GLint prevFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        mpv_opengl_fbo fbo{};
        fbo.fbo             = (int)m_fb.m_iFb;
        fbo.w               = W;
        fbo.h               = H;
        fbo.internal_format = 0;

        int flipY = 0;       // hyprlock composites textures with TRANSFORM_FLIPPED_180; leaving mpv's
                             // native FBO orientation matches the image-asset convention (verified upright).
        int blockForTime = 0; // don't let mpv block the main thread waiting for A/V target time - we pace off vsync.

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTime},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        const int RENDERED = mpv_render_context_render(m_glCtx, params);

        // Restore hyprlock's GL state (mpv resets most state to GL defaults; these are the ones we depend on).
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DITHER); // mpv disables GL_DITHER at init and never restores it
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);

        if (RENDERED < 0) {
            if (!m_renderErrLogged) {
                Log::logger->log(Log::ERR, "[mpv] mpv_render_context_render failed: {}", mpv_error_string(RENDERED));
                m_renderErrLogged = true;
            }
        } else if (HASNEW)
            m_hasFrame = true;
    }

    // Watchdog: a file that opens but never produces a frame (no video track, wedged decoder) must not
    // keep the lock surface spinning at refresh rate forever. Give up after a timeout and fall back to color.
    if (!m_hasFrame && std::chrono::steady_clock::now() - m_initTime > MPV_FIRST_FRAME_TIMEOUT) {
        Log::logger->log(Log::ERR, "[mpv] no video frame after {}s ({}); does it have a video track? Falling back to background color.",
                         std::chrono::duration_cast<std::chrono::seconds>(MPV_FIRST_FRAME_TIMEOUT).count(), m_path);
        m_failed = true;
        return nullptr;
    }

    return m_hasFrame ? &m_fb.m_cTex : nullptr;
}

const CTexture* CMpvVideo::lastFrame() const {
    return m_hasFrame ? &m_fb.m_cTex : nullptr;
}

void CMpvVideo::setPaused(bool paused) {
    if (m_paused == paused)
        return;

    m_paused = paused;
    if (m_mpv)
        mpv_set_property_string(m_mpv, "pause", paused ? "yes" : "no");
}

void CMpvVideo::cleanup() {
    // GL teardown must run with an EGL context current. Widgets are destroyed in
    // g_pRenderer.reset(), after all lock surfaces (and their EGL surfaces) are gone but
    // before g_pEGL.reset() - so bind the context surfacelessly before freeing GL objects.
    if (g_pEGL)
        g_pEGL->makeCurrent(nullptr);

    if (m_glCtx) {
        mpv_render_context_free(m_glCtx); // frees mpv's GL objects; blocks until callbacks settle
        m_glCtx = nullptr;
    }

    if (m_mpv) {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
    }
    // m_fb is freed by its destructor, with the context still current from above.
}

#endif // HYPRLOCK_WITH_MPV
