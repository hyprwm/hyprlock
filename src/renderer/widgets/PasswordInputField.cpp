#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>

CPasswordInputField::CPasswordInputField(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props) {
    size        = std::any_cast<Hyprlang::VEC2>(props.at("size"));
    inner       = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
    outer       = std::any_cast<Hyprlang::INT>(props.at("outer_color"));
    out_thick   = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
    fadeOnEmpty = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
    font        = std::any_cast<Hyprlang::INT>(props.at("font_color"));
    pos         = std::any_cast<Hyprlang::VEC2>(props.at("position"));

    pos = posFromHVAlign(viewport, size, pos, std::any_cast<Hyprlang::STRING>(props.at("halign")), std::any_cast<Hyprlang::STRING>(props.at("valign")));

    std::string placeholderText = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));
    if (!placeholderText.empty()) {
        placeholder.resourceID = "placeholder:" + std::to_string((uintptr_t)this);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = placeholder.resourceID;
        request.asset                = placeholderText;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = std::string{"Sans"};
        request.props["color"]       = CColor{1.0 - font.r, 1.0 - font.g, 1.0 - font.b, 0.5};
        request.props["font_size"]   = 12;
        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }
}

void CPasswordInputField::updateFade() {
    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    if (!fadeOnEmpty) {
        fade.a = 1.0;
        return;
    }

    if (PASSLEN == 0 && fade.a != 0.0 && (!fade.animated || fade.appearing)) {
        fade.a         = 1.0;
        fade.animated  = true;
        fade.appearing = false;
        fade.start     = std::chrono::system_clock::now();
    } else if (PASSLEN > 0 && fade.a != 1.0 && (!fade.animated || !fade.appearing)) {
        fade.a         = 0.0;
        fade.animated  = true;
        fade.appearing = true;
        fade.start     = std::chrono::system_clock::now();
    }

    if (fade.animated) {
        if (fade.appearing)
            fade.a = std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - fade.start).count() / 100000.0, 0.0, 1.0);
        else
            fade.a = std::clamp(1.0 - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - fade.start).count() / 100000.0, 0.0, 1.0);

        if ((fade.appearing && fade.a == 1.0) || (!fade.appearing && fade.a == 0.0))
            fade.animated = false;
    }
}

void CPasswordInputField::updateDots() {
    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    if (PASSLEN == dots.currentAmount)
        return;

    const auto  DELTA = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dots.lastFrame).count();

    const float TOADD = DELTA / 1000000.0 * (dots.speedPerSecond * std::clamp(std::abs(PASSLEN - dots.currentAmount), 0.5f, INFINITY));

    if (PASSLEN > dots.currentAmount) {
        dots.currentAmount += TOADD;
        if (dots.currentAmount > PASSLEN)
            dots.currentAmount = PASSLEN;
    } else if (PASSLEN < dots.currentAmount) {
        dots.currentAmount -= TOADD;
        if (dots.currentAmount < PASSLEN)
            dots.currentAmount = PASSLEN;
    }

    dots.lastFrame = std::chrono::system_clock::now();
}

bool CPasswordInputField::draw(const SRenderData& data) {
    CBox inputFieldBox = {pos, size};
    CBox outerBox      = {pos - Vector2D{out_thick, out_thick}, size + Vector2D{out_thick * 2, out_thick * 2}};

    updateFade();
    updateDots();

    CColor outerCol = outer;
    outer.a         = fade.a * data.opacity;
    CColor innerCol = inner;
    innerCol.a      = fade.a * data.opacity;

    g_pRenderer->renderRect(outerBox, outerCol, outerBox.h / 2.0);
    g_pRenderer->renderRect(inputFieldBox, innerCol, inputFieldBox.h / 2.0);

    constexpr int PASS_SPACING = 3;
    constexpr int PASS_SIZE    = 8;

    for (size_t i = 0; i < std::floor(dots.currentAmount); ++i) {
        Vector2D currentPos = inputFieldBox.pos() + Vector2D{PASS_SPACING * 2, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} + Vector2D{(PASS_SIZE + PASS_SPACING) * i, 0};
        CBox     box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
        CColor   fontCol = font;
        g_pRenderer->renderRect(box, fontCol, PASS_SIZE / 2.0);
    }

    if (dots.currentAmount != std::floor(dots.currentAmount)) {
        Vector2D currentPos =
            inputFieldBox.pos() + Vector2D{PASS_SPACING * 2, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} + Vector2D{(PASS_SIZE + PASS_SPACING) * std::floor(dots.currentAmount), 0};
        CBox   box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
        CColor fontCol = font;
        fontCol.a      = (dots.currentAmount - std::floor(dots.currentAmount)) * data.opacity;
        g_pRenderer->renderRect(box, fontCol, PASS_SIZE / 2.0);
    }

    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    if (PASSLEN == 0 && !placeholder.resourceID.empty()) {
        if (!placeholder.asset)
            placeholder.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

        if (placeholder.asset) {
            Vector2D pos = outerBox.pos() + outerBox.size() / 2.f;
            pos          = pos - placeholder.asset->texture.m_vSize / 2.f;
            CBox textbox{pos, placeholder.asset->texture.m_vSize};
            g_pRenderer->renderTexture(textbox, placeholder.asset->texture, data.opacity * fade.a, 0);
        }
    }

    return dots.currentAmount != PASSLEN || data.opacity < 1.0 || fade.a < 1.0;
}