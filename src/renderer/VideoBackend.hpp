#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include <filesystem>
#include <unordered_set>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// Handles FFmpeg video decoding on a background thread.
// CBackground owns one of these when path is a video file.
// All GL upload and rendering stays in CBackground.
class CVideoBackend {
  public:
    ~CVideoBackend();

    // Returns true if the file extension is a recognised video format.
    static bool isVideoFile(const std::string& path);

    // Open the file and start the decode thread. Returns false on failure.
    bool open(const std::string& path);

    // Stop the decode thread and release all FFmpeg resources.
    void stop();

    // Swap the latest decoded RGBA frame into buf (O(1), no memcpy).
    // Returns true if a new frame was available and buf was updated.
    bool swapFrame(std::vector<uint8_t>& buf);

    int  frameW()    const { return m_frameW; }
    int  frameH()    const { return m_frameH; }
    bool isRunning() const { return m_running; }

  private:
    void startDecodeThread();

    AVFormatContext*  m_formatCtx = nullptr;
    AVCodecContext*   m_codecCtx  = nullptr;
    SwsContext*       m_swsCtx    = nullptr;
    int               m_streamIdx = -1;
    int               m_frameW    = 0;
    int               m_frameH    = 0;
    double            m_timeBase  = 0.0;

    std::mutex           m_frameMutex;
    std::vector<uint8_t> m_frameData;
    bool                 m_hasNewFrame = false;

    std::chrono::steady_clock::time_point m_startTime;

    std::thread       m_decodeThread;
    std::atomic<bool> m_running{false};
};
