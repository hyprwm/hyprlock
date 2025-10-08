#pragma once

#include "../../defines.hpp"
#include "IWidget.hpp"
#include "Shadowable.hpp"
#include "../../core/Timer.hpp"
#include <hyprgraphics/resource/resources/AsyncResource.hpp>
#include <hyprgraphics/resource/resources/TextResource.hpp>
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
    virtual void onAssetUpdate(ResourceID id, ASP<CTexture> newAsset);

    virtual CBox getBoundingBoxWl() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual void onHover(const Vector2D& pos);

    void         reset();

    void         renderUpdate();
    void         onTimerUpdate();
    void         plantTimer();

  private:
    AWP<CLabel>                                    m_self;

    std::string                                    labelPreFormat;
    IWidget::SFormatResult                         label;

    std::string                                    halign, valign;
    std::string                                    onclickCommand;

    Vector2D                                       viewport;
    Vector2D                                       pos;
    Vector2D                                       configPos;
    double                                         angle;

    ResourceID                                     resourceID        = 0;
    bool                                           m_pendingResource = false;

    size_t                                         m_dynamicRevision = 0;

    ASP<CTexture>                                  asset = nullptr;

    std::string                                    outputStringPort;

    Hyprgraphics::CTextResource::STextResourceData request;

    ASP<CTimer>                                    labelTimer = nullptr;

    CShadowable                                    shadow;
    bool                                           updateShadow = true;
};
