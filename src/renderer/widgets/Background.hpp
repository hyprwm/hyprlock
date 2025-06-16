#pragma once

#include "IWidget.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "../Framebuffer.hpp"
#include "../AsyncResourceGatherer.hpp"
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

    void         registerSelf(const SP<CBackground>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);

    void         reset(); // Unload assets, remove timers, etc.

    void         renderRect(CHyprColor color);

    void         renderBlur(const CTexture& text, CFramebuffer& fb);

    void         onReloadTimerUpdate();
    void         plantReloadTimer();
    void         startCrossFade();

  private:
    WP<CBackground> m_self;

    // if needed
    UP<CFramebuffer>                        blurredFB;
    UP<CFramebuffer>                        pendingBlurredFB;

    int                                     blurSize          = 10;
    int                                     blurPasses        = 3;
    float                                   noise             = 0.0117;
    float                                   contrast          = 0.8916;
    float                                   brightness        = 0.8172;
    float                                   vibrancy          = 0.1696;
    float                                   vibrancy_darkness = 0.0;
    Vector2D                                viewport;
    std::string                             path = "";

    std::string                             outputPort;
    Hyprutils::Math::eTransform             transform;

    std::string                             resourceID;
    std::string                             scResourceID;
    std::string                             pendingResourceID;

    PHLANIMVAR<float>                       crossFadeProgress;

    CHyprColor                              color;
    SPreloadedAsset*                        asset        = nullptr;
    SPreloadedAsset*                        scAsset      = nullptr;
    SPreloadedAsset*                        pendingAsset = nullptr;
    bool                                    isScreenshot = false;
    bool                                    firstRender  = true;

    int                                     reloadTime = -1;
    std::string                             reloadCommand;
    CAsyncResourceGatherer::SPreloadRequest request;
    std::shared_ptr<CTimer>                 reloadTimer;
    std::filesystem::file_time_type         modificationTime;
};
