#pragma once

#include "../../defines.hpp"
#include "../../helpers/Math.hpp"
#include <string>
#include <unordered_map>
#include <any>

class COutput;

class IWidget {
  public:
    struct SRenderData {
        float opacity = 1;
    };

    virtual ~IWidget() = default;

    virtual void    configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput) = 0;
    virtual bool    draw(const SRenderData& data)                                                                = 0;

    static Vector2D posFromHVAlign(const Vector2D& viewport, const Vector2D& size, const Vector2D& offset, const std::string& halign, const std::string& valign,
                                   const double& ang = 0);
    static int      roundingForBox(const CBox& box, int roundingConfig);
    static int      roundingForBorderBox(const CBox& borderBox, int roundingConfig, int thickness);

    virtual CBox    getBoundingBoxWl() const {
        return CBox();
    };
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos) {}
    virtual void onHover(const Vector2D& pos) {}
    virtual bool onPointerMove(const Vector2D& pos);
    bool         containsPoint(const Vector2D& pos) const;

    struct SFormatResult {
        std::string formatted;
        float       updateEveryMs    = 0; // 0 means don't (static)
        bool        alwaysUpdate     = false;
        bool        cmd              = false;
        bool        allowForceUpdate = false;
    };

    static SFormatResult formatString(std::string in);

    void                 setHover(bool hover);
    bool                 isHovered() const;

  private:
    bool hovered = false;
};
