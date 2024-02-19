#include "Label.hpp"
#include "../../helpers/Color.hpp"
#include <hyprlang.hpp>
#include "../Renderer.hpp"
#include "../../helpers/Log.hpp"

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string CLabel::formatString(std::string in) {
    replaceAll(in, "$USER", std::string{getlogin()});
    return in;
}

CLabel::CLabel(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props) {
    std::string                             labelPreFormat = std::any_cast<Hyprlang::STRING>(props.at("text"));
    std::string                             fontFamily     = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
    CColor                                  labelColor     = std::any_cast<Hyprlang::INT>(props.at("color"));
    int                                     fontSize       = std::any_cast<Hyprlang::INT>(props.at("font_size"));

    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = std::string{"label:"} + std::to_string((uintptr_t)this);
    resourceID                   = request.id;
    request.asset                = formatString(labelPreFormat);
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = labelColor;
    request.props["font_size"]   = fontSize;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    auto POS__ = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    pos        = {POS__.x, POS__.y};

    viewport = viewport_;
    label    = request.asset;

    halign = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign = std::any_cast<Hyprlang::STRING>(props.at("valign"));
}

bool CLabel::draw(const SRenderData& data) {
    if (!asset) {
        asset = g_pRenderer->asyncResourceGatherer->getAssetByID(resourceID);

        if (!asset)
            return true;

        // calc pos
        if (halign == "center")
            pos.x += viewport.x / 2.0 - asset->texture.m_vSize.x / 2.0;
        else if (halign == "left")
            pos.x += 0;
        else if (halign == "right")
            pos.x += viewport.x - asset->texture.m_vSize.x;
        else if (halign != "none")
            Debug::log(ERR, "Label: invalid halign {}", halign);

        if (valign == "center")
            pos.y += viewport.y / 2.0 - asset->texture.m_vSize.y / 2.0;
        else if (valign == "top")
            pos.y += viewport.y - asset->texture.m_vSize.y;
        else if (valign == "bottom")
            pos.y += asset->texture.m_vSize.y;
        else if (valign != "none")
            Debug::log(ERR, "Label: invalid halign {}", halign);
    }

    CBox box = {pos.x, pos.y, asset->texture.m_vSize.x, asset->texture.m_vSize.y};

    g_pRenderer->renderTexture(box, asset->texture, data.opacity);

    return false;
}