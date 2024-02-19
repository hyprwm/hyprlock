#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>

CPasswordInputField::CPasswordInputField(const Vector2D& viewport, const Vector2D& size_, const CColor& dot_color_, const CColor& outer_, const CColor& inner_, int out_thick_, bool fadeEmpty) {
    size        = size_;
    pos         = viewport / 2.f - size_ / 2.f;
    dot_color   = dot_color_;
    inner       = inner_;
    outer       = outer_;
    out_thick   = out_thick_;
    fadeOnEmpty = fadeEmpty;
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
        CColor dotCol = dot_color;
        dotCol.a = data.opacity;
        g_pRenderer->renderRect(box, dotCol, PASS_SIZE / 2.0);
    }

    if (dots.currentAmount != std::floor(dots.currentAmount)) {
        Vector2D currentPos =
            inputFieldBox.pos() + Vector2D{PASS_SPACING * 2, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} + Vector2D{(PASS_SIZE + PASS_SPACING) * std::floor(dots.currentAmount), 0};
        CBox box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
        CColor dotCol = dot_color;
        dotCol.a = (dots.currentAmount - std::floor(dots.currentAmount)) * data.opacity;
        g_pRenderer->renderRect(box, dotCol, PASS_SIZE / 2.0);
    }

    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    return dots.currentAmount != PASSLEN || data.opacity < 1.0 || fade.a < 1.0;
}
