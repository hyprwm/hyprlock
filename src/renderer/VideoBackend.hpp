#pragma once

#include <string>
#include <functional>

#include "Texture.hpp"
#include "../defines.hpp"

#ifdef HYPRLOCK_HAS_VIDEO

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// Decodes a video file with FFmpeg on a background thread and uploads the
// frames into a GL texture for CBackground to composite. Decoding prefers a
// hardware decoder when one is available and falls back to software.
//
// One backend is shared between all outputs showing the same file: acquire()
// hands out a refcounted instance from a path-keyed registry, and the backend
// stops when the last holder releases it.
//
// Threading contract:
//  - acquire(), updateTexture() and texture() must be called from the render
//    thread; updateTexture() additionally needs the EGL context current, and
//    releasing the last reference needs it for GL teardown.
//  - The file is opened and decoded entirely on the decode thread so a slow
//    or wedged source can never block the render thread. Failures degrade to
//    "no frames" and are logged.
//  - The decode thread owns all FFmpeg state and shares only
//    m_frameData and the publish/texture serial pair with the render thread,
//    under m_frameMutex.
//  - Frame listeners are invoked from the decode thread after each frame
//    publish; they must be thread-safe and must not capture objects they can
//    outlive.
class CVideoBackend {
  public:
    ~CVideoBackend();

    // Returns true if the file extension is a recognised video format.
    static bool isVideoFile(const std::string& path);

    // Return the backend already decoding absPath at this revision, or create
    // one and start its decode thread. revision distinguishes reloads of the
    // same path (overwritten file) so they get a fresh decoder even while
    // other outputs still hold the old one. Frames are converted at no more
    // than the aspect-fill size for the largest attached viewport (never
    // upscaled); attaching a larger output re-opens the stream with the grown
    // target.
    static SP<CVideoBackend> acquire(const std::string& absPath, size_t revision, const Vector2D& viewport);

    // Register a wake-up callback; the returned token removes it again. cb
    // fires on the decode thread after every published frame, e.g. to
    // schedule a redraw.
    uint64_t addFrameListener(std::function<void()> cb);
    void     removeFrameListener(uint64_t token);

    // Upload the newest decoded frame into texture(), if one is pending.
    // EGL context must be current. With several outputs sharing the backend
    // only the first caller per frame uploads; compare textureSerial() to
    // detect the change.
    void updateTexture();

    // Monotonic id of the frame currently in texture(). Outputs compare this
    // against their last-seen value to know when to re-blur.
    uint64_t textureSerial() const {
        return m_textureSerial;
    }

    // Whether texture() holds at least one decoded frame.
    bool hasFrame() const {
        return m_texture.m_bAllocated;
    }

    const CTexture& texture() const {
        return m_texture;
    }

    // Rotation from the stream's display matrix (0/90/180/270, clockwise).
    int rotationDegrees() const {
        return m_rotation;
    }

    // Whether the decoded frames carry an alpha channel (frames are published
    // premultiplied; the caller should render the background color underneath).
    bool hasAlpha() const {
        return m_hasAlpha;
    }

  private:
    CVideoBackend() = default;

    // Start the decode thread for path; registry-only, via acquire().
    void                 open(const std::string& path, const Vector2D& viewport);
    // Stop the decode thread and release all FFmpeg and GL resources.
    // Listener registrations survive so open() can be called again.
    void                 stop();
    // Grow the conversion target to cover a larger attaching output.
    void                 ensureViewport(const Vector2D& viewport);

    bool                 openStream();
    void                 initHwDecode(const AVCodec* codec);
    void                 decodeLoop();
    void                 pacedWaitUntil(const std::chrono::steady_clock::time_point& tp);
    static int           interruptCallback(void* opaque);
    static AVPixelFormat hwGetFormat(AVCodecContext* ctx, const AVPixelFormat* fmts);

    AVFormatContext*     m_formatCtx   = nullptr;
    AVCodecContext*      m_codecCtx    = nullptr;
    SwsContext*          m_swsCtx      = nullptr;
    AVBufferRef*         m_hwDeviceCtx = nullptr;
    AVPixelFormat        m_hwPixFmt    = AV_PIX_FMT_NONE; // decode thread only after open
    int                            m_streamIdx = -1;
    int                            m_frameW    = 0;
    int                            m_frameH    = 0;
    std::string                    m_path;
    std::pair<std::string, size_t> m_registryKey; // (path, revision); set once by acquire()
    Vector2D                       m_viewportHint;

    // guarded by m_listenerMutex; iterated on the decode thread per publish
    std::mutex                                              m_listenerMutex;
    std::vector<std::pair<uint64_t, std::function<void()>>> m_frameListeners;
    uint64_t                                                m_nextListenerToken = 1;

    // decode thread only
    double                                m_timeBase = 0.0;
    int64_t                               m_startPts = 0; // stream start_time, subtracted from frame PTS for pacing
    std::chrono::steady_clock::duration   m_frameInterval{};
    AVColorSpace                          m_lastColorspace = AVCOL_SPC_UNSPECIFIED;
    AVColorRange                          m_lastRange      = AVCOL_RANGE_UNSPECIFIED;
    std::chrono::steady_clock::time_point m_startTime;

    // shared between decode and render thread, guarded by m_frameMutex.
    // m_publishSerial != m_textureSerial means a frame awaits upload; the
    // decode thread bumps the former, updateTexture() catches the latter up.
    // textureSerial() reads m_textureSerial lock-free, which is safe on the
    // render thread because that is the only thread writing it.
    std::mutex              m_frameMutex;
    std::condition_variable m_frameCV; // wakes a decode thread parked on backpressure
    std::vector<uint8_t>    m_frameData;
    uint64_t                m_publishSerial = 0;
    uint64_t                m_textureSerial = 0;

    // render thread only
    std::vector<uint8_t> m_uploadBuffer;
    CTexture             m_texture;

    std::thread             m_decodeThread;
    std::atomic<int>        m_rotation{0};
    std::atomic<bool>       m_hasAlpha{false};
    std::atomic<bool>       m_stopRequested{false};
    std::mutex              m_stopMutex;
    std::condition_variable m_stopCV;
};

#else // !HYPRLOCK_HAS_VIDEO

// Stub for builds without FFmpeg: isVideoFile() is statically false, so every
// video code path in the widgets is dead and compiled out - the widgets
// themselves need no #ifdefs.
class CVideoBackend {
  public:
    static bool isVideoFile(const std::string&) {
        return false;
    }

    static SP<CVideoBackend> acquire(const std::string&, size_t, const Vector2D&) {
        return SP<CVideoBackend>(new CVideoBackend());
    }

    uint64_t addFrameListener(std::function<void()>) {
        return 0;
    }
    void removeFrameListener(uint64_t) {}

    void updateTexture() {}
    uint64_t textureSerial() const {
        return 0;
    }
    bool hasFrame() const {
        return false;
    }
    const CTexture& texture() const {
        return m_texture;
    }
    int rotationDegrees() const {
        return 0;
    }
    bool hasAlpha() const {
        return false;
    }

  private:
    CVideoBackend() = default;

    CTexture m_texture;
};

#endif // HYPRLOCK_HAS_VIDEO
