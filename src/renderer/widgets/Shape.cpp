#include "Shape.hpp"
#include "../Renderer.hpp"
#include <cmath>

CShape::CShape(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props) : shadow(this, props, viewport_) {

    size        = std::any_cast<Hyprlang::VEC2>(props.at("size"));
    rounding    = std::any_cast<Hyprlang::INT>(props.at("rounding"));
    border      = std::any_cast<Hyprlang::INT>(props.at("border_size"));
    color       = std::any_cast<Hyprlang::INT>(props.at("color"));
    borderColor = std::any_cast<Hyprlang::INT>(props.at("border_color"));
    pos         = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    halign      = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign      = std::any_cast<Hyprlang::STRING>(props.at("valign"));
    angle       = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));
    xray        = std::any_cast<Hyprlang::INT>(props.at("xray"));

    viewport = viewport_;
    angle    = angle * M_PI / 180.0;

    const Vector2D VBORDER  = {border, border};
    const Vector2D REALSIZE = size + VBORDER * 2.0;

    pos = posFromHVAlign(viewport, xray ? size : (angle == 0 ? REALSIZE : REALSIZE + Vector2D{2.0, 2.0}), pos, halign, valign, xray ? 0 : angle);

    if (xray) {
        shapeBox  = {pos, size};
        borderBox = {pos - VBORDER, REALSIZE};
    } else {
        shapeBox  = {pos + VBORDER, size};
        borderBox = {pos, REALSIZE};
    }
}

bool CShape::draw(const SRenderData& data) {

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    CColor borderCol = borderColor;
    borderCol.a *= data.opacity;

    const auto MINHALFBORDER = std::min(borderBox.w, borderBox.h) / 2.0;

    if (xray) {
        if (border > 0) {
            const int PIROUND = std::min(MINHALFBORDER, std::round(border * M_PI));
            g_pRenderer->renderRect(borderBox, borderCol, rounding == -1 ? PIROUND : std::clamp(rounding, 0, PIROUND));
        }

        glEnable(GL_SCISSOR_TEST);
        glScissor(shapeBox.x, shapeBox.y, shapeBox.width, shapeBox.height);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        return data.opacity < 1.0;
    }

    const auto MINHALFSHAPE = std::min(shapeBox.w, shapeBox.h) / 2.0;
    const bool ALLOWROUND   = rounding > -1 && rounding < MINHALFSHAPE;
    const int  ROUNDSHAPE   = ALLOWROUND ? rounding : MINHALFSHAPE;
    const int  ROUNDBORDER  = ALLOWROUND ? (rounding == 0 ? 0 : rounding + std::round(border / M_PI)) : MINHALFBORDER;

    if (angle == 0) {
        CColor shapeCol = color;
        shapeCol.a *= data.opacity;

        if (border > 0)
            g_pRenderer->renderRect(borderBox, borderCol, ROUNDBORDER);

        g_pRenderer->renderRect(shapeBox, shapeCol, ROUNDSHAPE);

        return data.opacity < 1.0;
    }

    if (!shapeFB.isAllocated()) {
        borderBox.x = 1.0;
        borderBox.y = 1.0;
        shapeBox.x  = border + 1.0;
        shapeBox.y  = border + 1.0;

        shapeFB.alloc(borderBox.width + 2.0, borderBox.height + 2.0, true);
        g_pRenderer->pushFb(shapeFB.m_iFb);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (border > 0)
            g_pRenderer->renderRect(borderBox, borderColor, ROUNDBORDER);

        g_pRenderer->renderRect(shapeBox, color, ROUNDSHAPE);
        g_pRenderer->popFb();
    }

    CTexture* tex    = &shapeFB.m_cTex;
    CBox      texbox = {pos, tex->m_vSize};

    texbox.round();
    texbox.rot = angle;

    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, WL_OUTPUT_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}
