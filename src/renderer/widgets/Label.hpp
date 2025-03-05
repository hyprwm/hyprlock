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

using WidgetProps = std::unordered_map<std::string, std::any>;

class CLabel : public IWidget {
  public:
    CLabel(const Vector2D& viewport_, const WidgetProps& props_, const std::string& output);
    ~CLabel();

    virtual bool draw(const SRenderData& data);

    void         renderUpdate();
    void         onTimerUpdate();
    void         plantTimer();

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
    SPreloadedAsset*                        asset = nullptr;

    std::string                             outputStringPort;

    CAsyncResourceGatherer::SPreloadRequest request;

    std::shared_ptr<CTimer>                 labelTimer = nullptr;

    CShadowable                             shadow;
    bool                                    updateShadow = true;
    WidgetProps                             props;
};
