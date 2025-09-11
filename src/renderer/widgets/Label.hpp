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

    void         registerSelf(const ASP<CLabel>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);
    virtual CBox getBoundingBoxWl() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual void onHover(const Vector2D& pos);

    void         reset();

    void         renderUpdate();
    void         onTimerUpdate();
    void         plantTimer();

  private:
    AWP<CLabel>                             m_self;

    std::string                             getUniqueResourceId();

    std::string                             labelPreFormat;
    IWidget::SFormatResult                  label;

    Vector2D                                viewport;
    Vector2D                                pos;
    Vector2D                                configPos;
    double                                  angle;
    std::string                             resourceID;
    std::string                             pendingResourceID; // if dynamic label
    std::string                             halign, valign;
    std::string                             onclickCommand;
    std::shared_ptr<SPreloadedAsset>        asset = nullptr;

    std::string                             outputStringPort;

    CAsyncResourceGatherer::SPreloadRequest request;

    ASP<CTimer>                             labelTimer = nullptr;

    CShadowable                             shadow;
    bool                                    updateShadow = true;
};
