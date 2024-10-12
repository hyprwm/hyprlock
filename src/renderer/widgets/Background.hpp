#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../Framebuffer.hpp"
#include <string>
#include <unordered_map>
#include <any>

struct SPreloadedAsset;
class COutput;

class CBackground : public IWidget {
  public:
    CBackground(const Vector2D& viewport, COutput* output_, const std::string& resourceID, const std::unordered_map<std::string, std::any>& props, bool ss_);

    virtual bool draw(const SRenderData& data);
    void         renderRect(CColor color);

  private:
    // if needed
    CFramebuffer     blurredFB;

    int              blurSize          = 10;
    int              blurPasses        = 3;
    float            noise             = 0.0117;
    float            contrast          = 0.8916;
    float            brightness        = 0.8172;
    float            vibrancy          = 0.1696;
    float            vibrancy_darkness = 0.0;
    Vector2D         viewport;
    std::string      resourceID;
    CColor           color;
    SPreloadedAsset* asset        = nullptr;
    COutput*         output       = nullptr;
    bool             isScreenshot = false;
};