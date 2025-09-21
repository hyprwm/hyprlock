#pragma once

#include "../../defines.hpp"
#include "IWidget.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "../Framebuffer.hpp"
#include "../AsyncResourceGatherer.hpp"
#include <cstdint>
#include <hyprutils/math/Misc.hpp>
#include <string>
#include <unordered_map>
#include <any>
#include <chrono>
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

    // if needed
    UP<CFramebuffer>                         blurredFB;
    UP<CFramebuffer>                         pendingBlurredFB;
    UP<CFramebuffer>                         transformedScFB;

    int                                      blurSize          = 10;
    int                                      blurPasses        = 3;
    float                                    noise             = 0.0117;
    float                                    contrast          = 0.8916;
    float                                    brightness        = 0.8172;
    float                                    vibrancy          = 0.1696;
    float                                    vibrancy_darkness = 0.0;
    Vector2D                                 viewport;
    std::string                              path = "";

    std::string                              outputPort;
    Hyprutils::Math::eTransform              transform;

    std::string                              resourceID;
    std::string                              scResourceID;
    std::string                              pendingResourceID;

    PHLANIMVAR<float>                        crossFadeProgress;

    CHyprColor                               color;
    ASP<SPreloadedAsset>                     asset        = nullptr;
    ASP<SPreloadedAsset>                     scAsset      = nullptr;
    ASP<SPreloadedAsset>                     pendingAsset = nullptr;
    bool                                     isScreenshot = false;
    bool                                     firstRender  = true;

    int                                      reloadTime = -1;
    std::string                              reloadCommand;
    CAsyncResourceGatherer_::SPreloadRequest request;
    ASP<CTimer>                              reloadTimer;
    std::filesystem::file_time_type          modificationTime;
};
