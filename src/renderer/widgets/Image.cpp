#include "Image.hpp"
#include "../Renderer.hpp"
#include <cmath>

CImage::CImage(const Vector2D& viewport_, COutput* output_, const std::string& resourceID_, const std::unordered_map<std::string, std::any>& props) :
    viewport(viewport_), resourceID(resourceID_), output(output_), shadow(this, props, viewport_) {

    size     = std::any_cast<Hyprlang::INT>(props.at("size"));
    rounding = std::any_cast<Hyprlang::INT>(props.at("rounding"));
    border   = std::any_cast<Hyprlang::INT>(props.at("border_size"));
    color    = std::any_cast<Hyprlang::INT>(props.at("border_color"));
    pos      = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    halign   = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign   = std::any_cast<Hyprlang::STRING>(props.at("valign"));
    angle    = std::any_cast<Hyprlang::FLOAT>(props.at("rotate"));

    angle = angle * M_PI / 180.0;
}

bool CImage::draw(const SRenderData& data) {

    if (resourceID.empty())
        return false;

    if (!asset)
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

    if (!asset)
        return true;

    if (asset->texture.m_iType == TEXTURE_INVALID) {
        g_pRenderer->asyncResourceGatherer->unloadAsset(asset);
        resourceID = "";
        return false;
    }

    if (!imageFB.isAllocated()) {

        const Vector2D TEXSIZE = asset->texture.m_vSize;
        const float    SCALEX  = size / TEXSIZE.x;
        const float    SCALEY  = size / TEXSIZE.y;

        // image with borders offset
        CBox texbox = {{border, border}, TEXSIZE};

        texbox.w *= std::max(SCALEX, SCALEY);
        texbox.h *= std::max(SCALEX, SCALEY);

        const bool ALLOWROUND = rounding > -1 && rounding < std::min(texbox.w, texbox.h) / 2.0;

        // plus borders if any
        CBox borderBox = {{}, {texbox.w + border * 2.0, texbox.h + border * 2.0}};

        borderBox.round();
        imageFB.alloc(borderBox.w, borderBox.h, true);
        g_pRenderer->pushFb(imageFB.m_iFb);

        if (border > 0)
            g_pRenderer->renderRect(borderBox, color, ALLOWROUND ? rounding : std::min(borderBox.w, borderBox.h) / 2.0);

        texbox.round();
        g_pRenderer->renderTexture(texbox, asset->texture, 1.0, ALLOWROUND ? rounding : std::min(texbox.w, texbox.h) / 2.0, WL_OUTPUT_TRANSFORM_NORMAL);
        g_pRenderer->popFb();
    }

    CTexture* tex    = &imageFB.m_cTex;
    CBox      texbox = {{}, tex->m_vSize};

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    shadow.draw(data);

    const auto TEXPOS = posFromHVAlign(viewport, tex->m_vSize, pos, halign, valign, angle);

    texbox.x = TEXPOS.x;
    texbox.y = TEXPOS.y;

    texbox.round();
    texbox.rot = angle;
    g_pRenderer->renderTexture(texbox, *tex, data.opacity, 0, WL_OUTPUT_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}
