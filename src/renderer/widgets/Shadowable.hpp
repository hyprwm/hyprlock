#pragma once

#include "../Framebuffer.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "IWidget.hpp"

#include <string>
#include <unordered_map>
#include <any>

class CShadowable {
  public:
    CShadowable(IWidget* widget_, const std::unordered_map<std::string, std::any>& props, const Vector2D& viewport_ /* TODO: make this not the entire viewport */);

    // instantly re-renders the shadow using the widget's draw() method
    void         markShadowDirty();
    virtual bool draw(const IWidget::SRenderData& data);

  private:
    IWidget*   widget = nullptr;
    int        size   = 10;
    int        passes = 4;
    float      boostA = 1.0;
    CHyprColor color{0, 0, 0, 1.0};
    Vector2D   viewport;

    // to avoid recursive shadows
    bool         ignoreDraw = false;

    CFramebuffer shadowFB;
};