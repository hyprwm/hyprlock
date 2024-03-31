#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
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
    CImage(const Vector2D& viewport, COutput* output_, const std::string& resourceID, const std::unordered_map<std::string, std::any>& props);
    ~CImage();

    virtual bool draw(const SRenderData& data);

    void         renderSuper();
    void         onTimerUpdate();
    void         plantTimer();

  private:
    CFramebuffer                            imageFB;

    int                                     size;
    int                                     rounding;
    double                                  border;
    double                                  angle;
    CColor                                  color;
    Vector2D                                pos;

    std::string                             halign, valign, path;

    bool                                    firstRender = true;

    int                                     reloadTime;
    std::string                             reloadCommand;
    std::filesystem::file_time_type         modificationTime;
    std::shared_ptr<CTimer>                 imageTimer;
    CAsyncResourceGatherer::SPreloadRequest request;

    Vector2D                                viewport;
    std::string                             resourceID;
    std::string                             pendingResourceID; // if reloading image
    SPreloadedAsset*                        asset  = nullptr;
    COutput*                                output = nullptr;
    CShadowable                             shadow;
};
