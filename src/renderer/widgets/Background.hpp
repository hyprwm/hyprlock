#pragma once

#include "IWidget.hpp"
#include "../../defines.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "../../helpers/Color.hpp"
#include "../../core/Timer.hpp"
#include "../Framebuffer.hpp"
#include "../VideoBackend.hpp"
#include <hyprutils/math/Misc.hpp>
#include <string>
#include <unordered_map>
#include <any>
#include <filesystem>

struct SPreloadedAsset;
class COutput;

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
    void            renderToFB(const CTexture& text, CFramebuffer& fb, int passes, Hyprutils::Math::eTransform tr = Hyprutils::Math::HYPRUTILS_TRANSFORM_NORMAL);

    void            onReloadTimerUpdate();
    void            plantReloadTimer();
    void            startCrossFade();

  private:
    AWP<CBackground> m_self;

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

    // Renders the lock fade-in fallback (screenshot crossfading to the solid
    // color, or just the color). Returns true while the fade still animates.
    bool renderFallback(const SRenderData& data);

    // Video playback (no-ops via the CVideoBackend stub when built without FFmpeg)
    void startVideo();
    void stopVideo();
    bool drawVideo(const SRenderData& data);

    // Video mode owns the blur FBs: kill any in-flight image transition
    // (crossfade animation, pending asset and its FB) before entering it.
    void              discardPendingImage();

    // Shared with other outputs showing the same file; the backend stops when
    // the last holder releases it.
    SP<CVideoBackend> m_videoBackend;
    uint64_t          m_videoFrameSerial   = 0;
    uint64_t          m_videoListenerToken = 0;
};
