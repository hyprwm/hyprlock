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

    CTexture* tex    = &asset->texture;
    CBox      texbox = {{}, tex->m_vSize};

    if (firstRender) {
        firstRender = false;
        shadow.markShadowDirty();
    }

    const float SCALEX = size / tex->m_vSize.x;
    const float SCALEY = size / tex->m_vSize.y;

    texbox.w *= std::max(SCALEX, SCALEY);
    texbox.h *= std::max(SCALEX, SCALEY);

    shadow.draw(data);

    const bool ALLOWROUND = rounding > -1 && rounding < std::min(texbox.w, texbox.h) / 2.0;
    const auto TEXPOS     = posFromHVAlign(viewport, Vector2D{texbox.w, texbox.h}, pos, halign, valign);

    texbox.x = TEXPOS.x;
    texbox.y = TEXPOS.y;

    if (border > 0) {
        CBox   borderBox = {TEXPOS - Vector2D{(double)border, (double)border}, texbox.size() + Vector2D{(double)border * 2.0, (double)border * 2.0}};
        CColor borderCol = color;
        borderCol.a *= data.opacity;
        borderBox.round();
        g_pRenderer->renderRect(borderBox, borderCol, ALLOWROUND ? rounding : std::min(borderBox.w, borderBox.h) / 2.0);
    }

    texbox.round();
    g_pRenderer->renderTexture(texbox, *tex, data.opacity, ALLOWROUND ? rounding : std::min(texbox.w, texbox.h) / 2.0, WL_OUTPUT_TRANSFORM_FLIPPED_180);

    return data.opacity < 1.0;
}
