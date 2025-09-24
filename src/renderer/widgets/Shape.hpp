#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "Shadowable.hpp"
#include <hyprutils/math/Box.hpp>
#include <string>
#include <unordered_map>
#include <any>

class CShape : public IWidget {
  public:
    CShape()          = default;
    virtual ~CShape() = default;

    void         registerSelf(const ASP<CShape>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);
    virtual void onAssetUpdate(ASP<CTexture> newAsset);

    virtual CBox getBoundingBoxWl() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual void onHover(const Vector2D& pos);

  private:
    AWP<CShape>        m_self;

    CFramebuffer       shapeFB;

    int                rounding;
    double             border;
    double             angle;
    CHyprColor         color;
    CGradientValueData borderGrad;
    Vector2D           size;
    Vector2D           pos;
    CBox               shapeBox;
    CBox               borderBox;
    bool               xray;

    std::string        halign, valign;

    bool               firstRender = true;
    std::string        onclickCommand;

    Vector2D           viewport;
    CShadowable        shadow;
};
