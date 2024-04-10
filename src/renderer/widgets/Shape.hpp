#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Box.hpp"
#include "Shadowable.hpp"
#include <string>
#include <unordered_map>
#include <any>

class CShape : public IWidget {
  public:
    CShape(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props);

    virtual bool draw(const SRenderData& data);

  private:
    CFramebuffer shapeFB;

    int          rounding;
    double       border;
    double       angle;
    CColor       color;
    CColor       borderColor;
    Vector2D     size;
    Vector2D     pos;
    CBox         shapeBox;
    CBox         borderBox;
    bool         xray;

    std::string  halign, valign;

    bool         firstRender = true;

    Vector2D     viewport;
    CShadowable  shadow;
};
