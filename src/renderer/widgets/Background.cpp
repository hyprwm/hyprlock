#include "Background.hpp"
#include "../Renderer.hpp"

CBackground::CBackground(const Vector2D& viewport_, const std::string& resourceID_, const std::unordered_map<std::string, std::any>& props) :
    viewport(viewport_), resourceID(resourceID_) {

    color             = std::any_cast<Hyprlang::INT>(props.at("color"));
    blurPasses        = std::any_cast<Hyprlang::INT>(props.at("blur_passes"));
    blurSize          = std::any_cast<Hyprlang::INT>(props.at("blur_size"));
    vibrancy          = std::any_cast<Hyprlang::FLOAT>(props.at("vibrancy"));
    vibrancy_darkness = std::any_cast<Hyprlang::FLOAT>(props.at("vibrancy_darkness"));
    noise             = std::any_cast<Hyprlang::FLOAT>(props.at("noise"));
    brightness        = std::any_cast<Hyprlang::FLOAT>(props.at("brightness"));
    contrast          = std::any_cast<Hyprlang::FLOAT>(props.at("contrast"));
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

    if (blurPasses > 0 && !blurredFB.isAllocated()) {
        // make it brah
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
        texbox.round();
        blurredFB.alloc(viewport.x, viewport.y); // TODO 10 bit
        blurredFB.bind();
        g_pRenderer->renderTexture(texbox, asset->texture, 1.0, 0,
                                   true); // this could be omitted but whatever it's only once and makes code cleaner plus less blurring on large texs
        g_pRenderer->blurFB(blurredFB, CRenderer::SBlurParams{blurSize, blurPasses, noise, contrast, brightness, vibrancy, vibrancy_darkness});
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    CTexture* tex = blurredFB.isAllocated() ? &blurredFB.m_cTex : &asset->texture;

    CBox      texbox = {{}, tex->m_vSize};

    Vector2D  size   = tex->m_vSize;
    float     scaleX = viewport.x / tex->m_vSize.x;
    float     scaleY = viewport.y / tex->m_vSize.y;

    texbox.w *= std::max(scaleX, scaleY);
    texbox.h *= std::max(scaleX, scaleY);

    if (scaleX > scaleY)
        texbox.y = -(texbox.h - viewport.y) / 2.f;
    else
        texbox.x = -(texbox.w - viewport.x) / 2.f;
    texbox.round();
    g_pRenderer->renderTexture(texbox, *tex, data.opacity);

    return data.opacity < 1.0;
}