#include "Shadowable.hpp"
#include "../Renderer.hpp"

CShadowable::CShadowable(IWidget* widget_, const std::unordered_map<std::string, std::any>& props, const Vector2D& viewport_) : widget(widget_), viewport(viewport_) {
    size   = std::any_cast<Hyprlang::INT>(props.at("shadow_size"));
    passes = std::any_cast<Hyprlang::INT>(props.at("shadow_passes"));
    color  = std::any_cast<Hyprlang::INT>(props.at("shadow_color"));
    boostA = std::any_cast<Hyprlang::FLOAT>(props.at("shadow_boost"));
}

void CShadowable::markShadowDirty() {

    if (passes == 0)
        return;

    if (!shadowFB.isAllocated())
        shadowFB.alloc(viewport.x, viewport.y, true);

    g_pRenderer->pushFb(shadowFB.m_iFb);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    ignoreDraw = true;
    widget->draw(IWidget::SRenderData{.opacity = 1.0});
    ignoreDraw = false;

    g_pRenderer->blurFB(shadowFB, CRenderer::SBlurParams{.size = size, .passes = passes, .colorize = color, .boostA = boostA});

    g_pRenderer->popFb();
}

bool CShadowable::draw(const IWidget::SRenderData& data) {
    if (passes == 0)
        return true;

    if (!shadowFB.isAllocated() || ignoreDraw)
        return true;

    CBox box = {0, 0, viewport.x, viewport.y};
    g_pRenderer->renderTexture(box, shadowFB.m_cTex, data.opacity, 0, WL_OUTPUT_TRANSFORM_NORMAL);
    return true;
}