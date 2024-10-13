#pragma once

#include "../../helpers/Math.hpp"
#include <string>

class IWidget {
  public:
    struct SRenderData {
        float opacity = 1;
    };
    virtual ~IWidget() = default;

    virtual bool     draw(const SRenderData& data) = 0;

    virtual Vector2D posFromHVAlign(const Vector2D& viewport, const Vector2D& size, const Vector2D& offset, const std::string& halign, const std::string& valign,
                                    const double& ang = 0);

    struct SFormatResult {
        std::string formatted;
        float       updateEveryMs    = 0; // 0 means don't (static)
        bool        alwaysUpdate     = false;
        bool        cmd              = false;
        bool        allowForceUpdate = false;
    };

    virtual SFormatResult formatString(std::string in);
};
