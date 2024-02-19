#include "Background.hpp"
#include "../Renderer.hpp"

CBackground::CBackground(const Vector2D& viewport_, const std::string& resourceID_) : viewport(viewport_), resourceID(resourceID_) {
    ;
}

bool CBackground::draw(const SRenderData& data) {
    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return false;

    CBox monbox = {0, 0, viewport.x, viewport.y};

    g_pRenderer->renderTexture(monbox, asset->texture, data.opacity);

    return data.opacity < 1.0;
}