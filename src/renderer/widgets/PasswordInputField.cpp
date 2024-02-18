#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>

CPasswordInputField::CPasswordInputField(const Vector2D& viewport, const Vector2D& size_, const CColor& outer_, const CColor& inner_, int out_thick_) {
    size      = size_;
    pos       = viewport / 2.f - size_ / 2.f;
    inner     = inner_;
    outer     = outer_;
    out_thick = out_thick_;
}

void CPasswordInputField::updateDots() {
    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    size_t     dotsAppearingOrPresent = std::count_if(dots.begin(), dots.end(), [](const auto& dot) { return dot.appearing || !dot.animated; });

    if (dotsAppearingOrPresent < PASSLEN) {
        dots.push_back(dot{.idx = dotsAppearingOrPresent + 1, .appearing = true, .animated = true, .a = 0, .start = std::chrono::system_clock::now()});
    } else if (dotsAppearingOrPresent > PASSLEN) {
        dots[dots.size() - 1].animated  = true;
        dots[dots.size() - 1].appearing = false;
        dots[dots.size() - 1].start     = std::chrono::system_clock::now();
    }

    for (auto& dot : dots) {
        if (dot.appearing) {
            if (dot.a < 1.0)
                dot.a = std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dot.start).count() / 100000.0, 0.0, 1.0);
        } else {
            if (dot.a > 0.0)
                dot.a = std::clamp(1.0 - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dot.start).count() / 100000.0, 0.0, 1.0);
        }

        if (dot.appearing && dot.a == 1.0)
            dot.animated = false;
    }

    std::erase_if(dots, [](const auto& dot) { return !dot.appearing && dot.a == 0.0; });
}

bool CPasswordInputField::draw() {
    CBox inputFieldBox = {pos, size};
    CBox outerBox      = {pos - Vector2D{out_thick, out_thick}, size + Vector2D{out_thick * 2, out_thick * 2}};

    updateDots();

    g_pRenderer->renderRect(outerBox, outer, outerBox.h / 2.0);
    g_pRenderer->renderRect(inputFieldBox, inner, inputFieldBox.h / 2.0);

    constexpr int PASS_SPACING = 3;
    constexpr int PASS_SIZE    = 8;

    for (size_t i = 0; i < dots.size(); ++i) {
        Vector2D currentPos = inputFieldBox.pos() + Vector2D{PASS_SPACING, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} + Vector2D{(PASS_SIZE + PASS_SPACING) * dots[i].idx, 0};
        CBox     box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
        g_pRenderer->renderRect(box, CColor{0, 0, 0, dots[i].a}, PASS_SIZE / 2.0);
    }

    return std::ranges::any_of(dots.begin(), dots.end(), [](const auto& dot) { return dot.animated; });
}