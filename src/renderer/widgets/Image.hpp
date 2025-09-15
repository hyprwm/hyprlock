#pragma once

#include "../../defines.hpp"
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

    void         registerSelf(const ASP<CImage>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);
    virtual CBox getBoundingBoxWl() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual void onHover(const Vector2D& pos);

    void         reset();

    void         renderUpdate();
    void         onTimerUpdate();
    void         plantTimer();

  private:
    AWP<CImage>                             m_self;

    CFramebuffer                            imageFB;

    int                                     size;
    int                                     rounding;
    double                                  border;
    double                                  angle;
    CGradientValueData                      color;
    Vector2D                                pos;
    Vector2D                                configPos;

    std::string                             halign, valign, path;

    bool                                    firstRender = true;

    int                                     reloadTime;
    std::string                             reloadCommand;
    std::string                             onclickCommand;

    std::filesystem::file_time_type         modificationTime;
    ASP<CTimer>                             imageTimer;
    CAsyncResourceGatherer::SPreloadRequest request;

    Vector2D                                viewport;
    std::string                             stringPort;

    std::string                             resourceID;
    std::string                             pendingResourceID; // if reloading image
    ASP<SPreloadedAsset>                    asset = nullptr;
    CShadowable                             shadow;
};
