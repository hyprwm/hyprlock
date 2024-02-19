#include "Background.hpp"
#include "../Renderer.hpp"

CBackground::CBackground(const Vector2D& viewport_, const std::string& resourceID_, const CColor& color_) : viewport(viewport_), resourceID(resourceID_), color(color_) {
    ;
}

bool CBackground::draw(const SRenderData& data) {

    if (resourceID.empty()) {
        CBox   monbox = {0, 0, viewport.x, viewport.y};
        CColor col    = color;
        col.a *= data.opacity;
        g_pRenderer->renderRect(monbox, col, 0);
        return data.opacity < 1.0;
    }

    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return false;

    CBox monbox = {0, 0, viewport.x, viewport.y};

    g_pRenderer->renderTexture(monbox, asset->texture, data.opacity);

    return data.opacity < 1.0;
}