#pragma once

#include "IWidget.hpp"
#include "Shadowable.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "../AsyncResourceGatherer.hpp"
#include <string>
#include <unordered_map>
#include <any>

struct SPreloadedAsset;
class CSessionLockSurface;

class CLabel : public IWidget {
  public:
    CLabel() = default;
    ~CLabel();

    void registerSelf(const SP<CLabel>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput) override;
    virtual bool draw(const SRenderData& data) override;
    virtual std::string type() const override; // Added for layered rendering

    void reset();
    void renderUpdate();
    void onTimerUpdate();
    void plantTimer();

  private:
    WP<CLabel> m_self;

    std::string getUniqueResourceId();

    std::string labelPreFormat;
    IWidget::SFormatResult label;

    Vector2D viewport;
    Vector2D pos;
    Vector2D configPos;
    double angle;
    std::string resourceID;
    std::string pendingResourceID; // if dynamic label
    std::string halign, valign;
    SPreloadedAsset* asset = nullptr;
    std::string outputStringPort;

    CAsyncResourceGatherer::SPreloadRequest request;
    std::shared_ptr<CTimer> labelTimer = nullptr;

    CShadowable shadow;
    bool updateShadow = true;
};