#include "Shape.hpp"
#include "../Renderer.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/MiscFunctions.hpp"
#include <cmath>
#include <hyprlang.hpp>
#include <sys/types.h>

void CShape::registerSelf(const ASP<CShape>& self) {
    m_self = self;
}

void CShape::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    viewport = pOutput->getViewport();

    shadow.configure(m_self, props, viewport);

    try {
        size           = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(viewport);
        rounding       = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        border         = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        color          = std::any_cast<Hyprlang::INT>(props.at("color"));
        borderGrad     = *CGradientValueData::fromAnyPv(props.at("border_color"));
        pos            = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport);
        halign         = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign         = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        angle          = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));
        xray           = std::any_cast<Hyprlang::INT>(props.at("xray"));
        onclickCommand = std::any_cast<Hyprlang::STRING>(props.at("onclick"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CShape: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CShape: {}", e.what()); //
    }

    angle = angle * M_PI / 180.0;

    const Vector2D VBORDER  = {border, border};
    const Vector2D REALSIZE = size + VBORDER * 2.0;
    const Vector2D OFFSET   = angle == 0 ? Vector2D{0.0, 0.0} : Vector2D{1.0, 1.0};

    pos = posFromHVAlign(viewport, xray ? size : REALSIZE + OFFSET * 2.0, pos, halign, valign, xray ? 0 : angle);

    if (xray) {
        shapeBox  = {pos, size};
        borderBox = {pos - VBORDER, REALSIZE};
    } else {
        shapeBox  = {OFFSET + VBORDER, size};
        borderBox = {OFFSET, REALSIZE};
    }
}

bool CShape::draw(const SRenderData& data) {

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    const auto MINHALFBORDER = std::min(borderBox.w, borderBox.h) / 2.0;

    if (xray) {
        if (border > 0) {
            const int PIROUND = std::min(MINHALFBORDER, std::round(border * M_PI));
            g_pRenderer->renderBorder(borderBox, borderGrad, border, rounding == -1 ? PIROUND : std::clamp(rounding, 0, PIROUND), data.opacity);
        }

        glEnable(GL_SCISSOR_TEST);
        glScissor(shapeBox.x, shapeBox.y, shapeBox.width, shapeBox.height);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        return data.opacity < 1.0;
    }

    if (!shapeFB.isAllocated()) {
        const int ROUND       = roundingForBox(shapeBox, rounding);
        const int BORDERROUND = roundingForBorderBox(borderBox, rounding, border);
        Debug::log(LOG, "round: {}, borderround: {}", ROUND, BORDERROUND);

        shapeFB.alloc(borderBox.width + (borderBox.x * 2.0), borderBox.height + (borderBox.y * 2.0), true);
        g_pRenderer->pushFb(shapeFB.m_iFb);
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (border > 0)
            g_pRenderer->renderBorder(borderBox, borderGrad, border, BORDERROUND, 1.0);

        g_pRenderer->renderRect(shapeBox, color, ROUND);
        g_pRenderer->popFb();
    }

    CTexture* tex    = &shapeFB.m_cTex;
    CBox      texbox = {pos, tex->m_vSize};

    texbox.round();
    texbox.rot = angle;

    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}
CBox CShape::getBoundingBoxWl() const {
    return {
        Vector2D{pos.x, viewport.y - pos.y - size.y},
        size,
    };
}

void CShape::onClick(uint32_t button, bool down, const Vector2D& pos) {
    if (down && !onclickCommand.empty())
        spawnAsync(onclickCommand);
}

void CShape::onHover(const Vector2D& pos) {
    if (!onclickCommand.empty())
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
}
