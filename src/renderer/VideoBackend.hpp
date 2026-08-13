#pragma once

#include <string>
#include <functional>

#include "Texture.hpp"

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
// frames into a GL texture for CBackground to composite.
//
// Threading contract:
//  - open(), stop(), updateTexture() and texture() must be called from the
//    render thread; updateTexture() additionally needs the EGL context current.
//  - open() returns immediately; the file is opened and decoded entirely on
//    the decode thread so a slow or wedged source can never block the render
//    thread. Failures degrade to "no frames" and are logged.
//  - The decode thread owns all FFmpeg state and shares only
//    m_frameData/m_hasNewFrame with the render thread, under m_frameMutex.
//  - onFrame is invoked from the decode thread after each frame publish; it
//    must be thread-safe and must not capture objects it can outlive.
class CVideoBackend {
  public:
    ~CVideoBackend();

    // Returns true if the file extension is a recognised video format.
    static bool isVideoFile(const std::string& path);

    // Start the decode thread for path. Frames are converted at no more than
    // the aspect-fill size for viewport (never upscaled). onFrame fires (on
    // the decode thread) after every published frame, e.g. to schedule a redraw.
    void open(const std::string& path, const Vector2D& viewport, std::function<void()> onFrame);

    // Stop the decode thread and release all FFmpeg and GL resources.
    void stop();

    // Upload the newest decoded frame into texture(), if one is pending.
    // Returns true if a new frame was uploaded. EGL context must be current.
    bool updateTexture();

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
    bool                  openStream();
    void                  decodeLoop();
    void                  pacedWaitUntil(const std::chrono::steady_clock::time_point& tp);
    static int            interruptCallback(void* opaque);

    AVFormatContext*      m_formatCtx = nullptr;
    AVCodecContext*       m_codecCtx  = nullptr;
    SwsContext*           m_swsCtx    = nullptr;
    int                   m_streamIdx = -1;
    int                   m_frameW    = 0;
    int                   m_frameH    = 0;
    std::string           m_path;
    Vector2D              m_viewportHint;
    std::function<void()> m_onFrame;

    // decode thread only
    double                                m_timeBase = 0.0;
    int64_t                               m_startPts = 0; // stream start_time, subtracted from frame PTS for pacing
    std::chrono::steady_clock::duration   m_frameInterval{};
    AVColorSpace                          m_lastColorspace = AVCOL_SPC_UNSPECIFIED;
    AVColorRange                          m_lastRange      = AVCOL_RANGE_UNSPECIFIED;
    std::chrono::steady_clock::time_point m_startTime;

    // shared between decode and render thread, guarded by m_frameMutex
    std::mutex           m_frameMutex;
    std::vector<uint8_t> m_frameData;
    bool                 m_hasNewFrame = false;

    // render thread only
    std::vector<uint8_t>    m_uploadBuffer;
    CTexture                m_texture;

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

    void open(const std::string&, const Vector2D&, std::function<void()>) {}
    void stop() {}

    bool updateTexture() {
        return false;
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
    CTexture m_texture;
};

#endif // HYPRLOCK_HAS_VIDEO
