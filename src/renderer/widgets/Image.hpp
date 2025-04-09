#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../core/Timer.hpp"
#include "../AsyncResourceGatherer.hpp"
#include "Shadowable.hpp"
#include <string>
#include <filesystem>
#include <unordered_map>
#include <any>

struct SPreloadedAsset;
class COutput;

class CImage : public IWidget {
  public:
    CImage() = default;
    ~CImage();

    void registerSelf(const SP<CImage>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) override;
    virtual bool draw(const SRenderData& data) override;
    virtual std::string type() const override; // Added for layered rendering

    void reset();
    void renderUpdate();
    void onTimerUpdate();
    void plantTimer();

  private:
    WP<CImage> m_self;

    CFramebuffer imageFB;

    int size;
    int rounding;
    double border;
    double angle;
    CGradientValueData color;
    Vector2D pos;
    std::string halign, valign, path;
    bool firstRender = true;
    int reloadTime;
    std::string reloadCommand;
    std::filesystem::file_time_type modificationTime;
    std::shared_ptr<CTimer> imageTimer;
    CAsyncResourceGatherer::SPreloadRequest request;
    Vector2D viewport;
    std::string resourceID;
    std::string pendingResourceID; // if reloading image
    SPreloadedAsset* asset = nullptr;
    COutput* output = nullptr;
    CShadowable shadow;
};