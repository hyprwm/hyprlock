#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include "Shadowable.hpp"
#include <string>
#include <unordered_map>
#include <any>

struct SPreloadedAsset;
class COutput;

class CImage : public IWidget {
  public:
    CImage(const Vector2D& viewport, COutput* output_, const std::string& resourceID, const std::unordered_map<std::string, std::any>& props);

    virtual bool draw(const SRenderData& data);

  private:
    CFramebuffer     imageFB;

    int              size;
    int              rounding;
    double           border;
    double           angle;
    CColor           color;
    Vector2D         pos;

    std::string      halign, valign;

    bool             firstRender = true;

    Vector2D         viewport;
    std::string      resourceID;
    SPreloadedAsset* asset  = nullptr;
    COutput*         output = nullptr;
    CShadowable      shadow;
};
