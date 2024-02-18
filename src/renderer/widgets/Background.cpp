#include "Background.hpp"
#include "../Renderer.hpp"

CBackground::CBackground(const Vector2D& viewport_, const std::string& resourceID_) : viewport(viewport_), resourceID(resourceID_) {
    ;
}

bool CBackground::draw() {
    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return false;

    float bga    = std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - g_pRenderer->gatheredAt).count() / 500000.0, 0.0, 1.0);
    CBox  monbox = {0, 0, viewport.x, viewport.y};

    g_pRenderer->renderTexture(monbox, asset->texture, bga);

    return bga < 1.0;
}