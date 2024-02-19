#pragma once

#include "../../helpers/Vector2D.hpp"
#include <string>

class IWidget {
  public:
    struct SRenderData {
        float opacity = 1;
    };

    virtual bool     draw(const SRenderData& data) = 0;

    virtual Vector2D posFromHVAlign(const Vector2D& viewport, const Vector2D& size, const Vector2D& offset, const std::string& halign, const std::string& valign);
};