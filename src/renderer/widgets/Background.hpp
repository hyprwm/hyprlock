#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "../Framebuffer.hpp"
#include "../AsyncResourceGatherer.hpp"
#include <string>
#include <unordered_map>
#include <any>
#include <chrono>
#include <filesystem>

struct SPreloadedAsset;
class COutput;

struct SFade {
    std::chrono::system_clock::time_point start;
    float                                 a              = 0;
    std::shared_ptr<CTimer>               crossFadeTimer = nullptr;
};

class CBackground : public IWidget {
  public:
    CBackground(const Vector2D& viewport, COutput* output_, const std::string& resourceID, const std::unordered_map<std::string, std::any>& props, bool ss_);
    ~CBackground();

    virtual bool draw(const SRenderData& data);
    void         renderRect(CColor color);

    void         onReloadTimerUpdate();
    void         onCrossFadeTimerUpdate();
    void         plantReloadTimer();
    void         startCrossFadeOrUpdateRender();

  private:
    // if needed
    CFramebuffer                            blurredFB;

    int                                     blurSize          = 10;
    int                                     blurPasses        = 3;
    float                                   noise             = 0.0117;
    float                                   contrast          = 0.8916;
    float                                   brightness        = 0.8172;
    float                                   vibrancy          = 0.1696;
    float                                   vibrancy_darkness = 0.0;
    Vector2D                                viewport;
    std::string                             path = "";

    std::string                             resourceID;
    std::string                             pendingResourceID;

    float                                   crossFadeTime = -1.0;

    CColor                                  color;
    SPreloadedAsset*                        asset        = nullptr;
    COutput*                                output       = nullptr;
    bool                                    isScreenshot = false;
    SPreloadedAsset*                        pendingAsset = nullptr;
    bool                                    firstRender  = true;

    std::unique_ptr<SFade>                  fade;

    int                                     reloadTime;
    std::string                             reloadCommand;
    CAsyncResourceGatherer::SPreloadRequest request;
    std::shared_ptr<CTimer>                 reloadTimer;
    std::filesystem::file_time_type         modificationTime;
};
