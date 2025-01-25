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
    CLabel(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props, const std::string& output);
    ~CLabel();

    virtual bool draw(const SRenderData& data);

    void         renderUpdate();
    void         onTimerUpdate();
    void         plantTimer();
    CBox         getBoundingBox() const override;
    void         onClick(uint32_t button, bool down, const Vector2D& pos) override;
    void         onHover(const Vector2D& pos) override;

  private:
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
    SPreloadedAsset*                        asset = nullptr;

    std::string                             outputStringPort;

    CAsyncResourceGatherer::SPreloadRequest request;

    std::shared_ptr<CTimer>                 labelTimer = nullptr;

    CShadowable                             shadow;
    bool                                    updateShadow = true;
};
