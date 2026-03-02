#pragma once

#include "IWidget.hpp"
#include "../../defines.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "../../helpers/Color.hpp"
#include "../../core/Timer.hpp"
#include "../Framebuffer.hpp"
#include <hyprutils/math/Misc.hpp>
#include <string>
#include <unordered_map>
#include <any>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

struct SPreloadedAsset;
class COutput;

struct SVideoState {
    AVFormatContext*  formatCtx  = nullptr;
    AVCodecContext*   codecCtx   = nullptr;
    SwsContext*       swsCtx     = nullptr;
    int               streamIdx  = -1;
    int               frameW     = 0;
    int               frameH     = 0;
    double            timeBase   = 0.0; // seconds per PTS unit

    // Frame double-buffer (main thread swaps with its m_uploadBuffer)
    std::mutex           frameMutex;
    std::vector<uint8_t> frameData;    // latest RGBA frame
    bool                 hasNewFrame = false;

    std::chrono::steady_clock::time_point startTime;

    std::thread       decodeThread;
    std::atomic<bool> running{false};

    ~SVideoState(); // defined in .cpp so destructor sees complete ffmpeg types
};

class CBackground : public IWidget {
  public:
    CBackground();
    ~CBackground();

    void            registerSelf(const ASP<CBackground>& self);

    virtual void    configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput);
    virtual bool    draw(const SRenderData& data);
    virtual void    onAssetUpdate(ResourceID id, ASP<CTexture> newAsset);

    void            reset(); // Unload assets, remove timers, etc.

    void            updatePrimaryAsset();
    void            updatePendingAsset();
    void            updateScAsset();

    const CTexture& getPrimaryAssetTex() const;
    const CTexture& getPendingAssetTex() const;
    const CTexture& getScAssetTex() const;

    void            renderRect(CHyprColor color);
    void            renderToFB(const CTexture& text, CFramebuffer& fb, int passes, bool applyTransform = false);

    void            onReloadTimerUpdate();
    void            plantReloadTimer();
    void            startCrossFade();

  private:
    AWP<CBackground> m_self;

    // Video background support
    static bool isVideoFile(const std::string& path);
    bool        openVideo(const std::string& path);
    void        startVideoThread();
    void        stopVideo();

    // if needed
    UP<CFramebuffer>                blurredFB;
    UP<CFramebuffer>                pendingBlurredFB;
    UP<CFramebuffer>                transformedScFB;

    int                             blurSize          = 10;
    int                             blurPasses        = 3;
    float                           noise             = 0.0117;
    float                           contrast          = 0.8916;
    float                           brightness        = 0.8172;
    float                           vibrancy          = 0.1696;
    float                           vibrancy_darkness = 0.0;
    Vector2D                        viewport;
    std::string                     path = "";

    std::string                     outputPort;
    Hyprutils::Math::eTransform     transform;

    ResourceID                      resourceID      = 0;
    ResourceID                      scResourceID    = 0;
    bool                            pendingResource = false;

    PHLANIMVAR<float>               crossFadeProgress;

    CHyprColor                      color;
    ASP<CTexture>                   asset        = nullptr;
    ASP<CTexture>                   scAsset      = nullptr;
    ASP<CTexture>                   pendingAsset = nullptr;
    bool                            isScreenshot = false;
    bool                            firstRender  = true;

    int                             reloadTime = -1;
    std::string                     reloadCommand;
    ASP<CTimer>                     reloadTimer;
    std::filesystem::file_time_type modificationTime;
    size_t                          m_imageRevision = 0;

    // Video playback state
    bool                    m_isVideo     = false;
    UP<SVideoState>         m_video;
    CTexture                m_videoTexture;
    std::vector<uint8_t>    m_uploadBuffer; // O(1) swap target for decoded frames
};
