#include "Background.hpp"
#include "../Renderer.hpp"

CBackground::CBackground(const Vector2D& viewport_, const std::string& resourceID_, const CColor& color_) : viewport(viewport_), resourceID(resourceID_), color(color_) {
    ;
}

bool CBackground::draw(const SRenderData& data) {

    // Early fallback to background color if image does not exist
    if (resourceID.empty()) {
        return draw_color_bg(data);
    }

    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    // Fallback to background color if texture is invalid
    if (!asset || asset->valid)
        return draw_color_bg(data);

    CBox     texbox = {{}, asset->texture.m_vSize};

    Vector2D size   = asset->texture.m_vSize;
    float    scaleX = viewport.x / asset->texture.m_vSize.x;
    float    scaleY = viewport.y / asset->texture.m_vSize.y;

    texbox.w *= std::max(scaleX, scaleY);
    texbox.h *= std::max(scaleX, scaleY);

    if (scaleX > scaleY)
        texbox.y = -(texbox.h - viewport.y) / 2.f;
    else
        texbox.x = -(texbox.w - viewport.x) / 2.f;

    g_pRenderer->renderTexture(texbox, asset->texture, data.opacity);

    return data.opacity < 1.0;
}

bool CBackground::draw_color_bg(const SRenderData& data) {
    CBox   monbox = {0, 0, viewport.x, viewport.y};
    CColor col    = color;
    col.a *= data.opacity;
    g_pRenderer->renderRect(monbox, col, 0);
    return data.opacity < 1.0;
}
