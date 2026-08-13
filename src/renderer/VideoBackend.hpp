#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <chrono>

#include "Texture.hpp"

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
//  - The decode thread owns all FFmpeg state after open() returns and only
//    shares m_frameData/m_hasNewFrame with the render thread, under m_frameMutex.
class CVideoBackend {
  public:
    ~CVideoBackend();

    // Returns true if the file extension is a recognised video format.
    static bool isVideoFile(const std::string& path);

    // Open the file and start the decode thread. Returns false on failure.
    bool open(const std::string& path);

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

    // Whether the decode thread is still producing frames. False once it
    // exits on an unrecoverable error or an unseekable end of stream.
    bool isRunning() const {
        return m_threadAlive;
    }

  private:
    void             startDecodeThread();
    static int       interruptCallback(void* opaque);

    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext*  m_codecCtx  = nullptr;
    SwsContext*      m_swsCtx    = nullptr;
    int              m_streamIdx = -1;
    int              m_frameW    = 0;
    int              m_frameH    = 0;
    double           m_timeBase  = 0.0;
    int              m_rotation  = 0;
    std::string      m_path;

    // decode thread only
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
    std::atomic<bool>       m_stopRequested{false};
    std::atomic<bool>       m_threadAlive{false};
    std::mutex              m_stopMutex;
    std::condition_variable m_stopCV;
};
