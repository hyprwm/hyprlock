#pragma once

#include "../defines.hpp"
#include "Framebuffer.hpp"

#include <string>
#include <atomic>
#include <chrono>

struct mpv_handle;
struct mpv_render_context;

// Decodes a video file with libmpv and renders the current frame into an offscreen
// framebuffer whose texture can be composited like any other background texture.
//
// All GL-touching methods (renderFrame, and the destructor) MUST run on hyprlock's
// render thread with the EGL context current - i.e. from within a widget's draw() or
// during renderer teardown. mpv runs its own decode/demux threads internally; the only
// cross-thread surface is the render-update callback, which merely sets an atomic flag.
class CMpvVideo {
  public:
    CMpvVideo(const std::string& path, bool loop, bool mute, const std::string& hwdec);
    ~CMpvVideo();

    CMpvVideo(const CMpvVideo&)            = delete;
    CMpvVideo& operator=(const CMpvVideo&) = delete;

    // Renders the current frame into the internal FBO (sized to `size`) and returns its
    // texture. Returns nullptr while the first frame is not decoded yet. EGL context must
    // be current.
    const CTexture* renderFrame(const Vector2D& size);

    // The most recently rendered frame texture without advancing/redrawing mpv. Used to
    // recomposite an existing frame (e.g. when fps-capped). Returns nullptr if no frame yet.
    const CTexture* lastFrame() const;

    void            setPaused(bool paused);
    bool            failed() const {
        return m_failed;
    }
    // True once a non-looping video reached its end (frozen on the last frame).
    bool            ended() const {
        return m_ended;
    }

  private:
    bool                ensureInitialized();
    void                pumpEvents();
    void                cleanup();

    mpv_handle*         m_mpv   = nullptr;
    mpv_render_context* m_glCtx = nullptr;
    CFramebuffer        m_fb;

    std::atomic<bool>                     m_updateRequested = true; // set by mpv update callback (foreign thread)
    bool                                  m_initialized     = false;
    bool                                  m_failed          = false;
    bool                                  m_hasFrame        = false;
    bool                                  m_paused          = false;
    bool                                  m_ended           = false;
    bool                                  m_renderErrLogged = false;
    std::chrono::steady_clock::time_point m_initTime{}; // for the "no first frame" watchdog

    const std::string   m_path;
    const std::string   m_hwdec;
    const bool          m_loop;
    const bool          m_mute;

    static void         onMpvRenderUpdate(void* ctx);
};
