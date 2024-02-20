#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>

CPasswordInputField::CPasswordInputField(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props) {
    size                     = std::any_cast<Hyprlang::VEC2>(props.at("size"));
    inner                    = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
    outer                    = std::any_cast<Hyprlang::INT>(props.at("outer_color"));
    out_thick                = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
    dt_size                  = std::any_cast<Hyprlang::FLOAT>(props.at("dots_size"));
    dt_space                 = std::any_cast<Hyprlang::FLOAT>(props.at("dots_spacing"));
    fadeOnEmpty              = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
    font                     = std::any_cast<Hyprlang::INT>(props.at("font_color"));
    placeholder_color        = std::any_cast<Hyprlang::INT>(props.at("placeholder_color"));
    pos                      = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(props.at("hide_input"));
    viewport                 = viewport_;

    pos      = posFromHVAlign(viewport, size, pos, std::any_cast<Hyprlang::STRING>(props.at("halign")), std::any_cast<Hyprlang::STRING>(props.at("valign")));
    dt_size  = std::clamp(dt_size, 0.2f, 0.8f);
    dt_space = std::clamp(dt_space, 0.f, 1.f);

    std::string placeholderText = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));
    
    if (!placeholderText.empty()) {
        placeholder.resourceID = "placeholder:" + std::to_string((uintptr_t)this);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = placeholder.resourceID;
        request.asset                = placeholderText;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = std::string{"Sans"};
        request.props["color"]  = CColor{placeholder_color.r,placeholder_color.g,placeholder_color.b,0.5};
        request.props["font_size"]   = (int)size.y / 4;
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

    if (std::abs(PASSLEN - dots.currentAmount) > 1) {
        dots.currentAmount = std::clamp(dots.currentAmount, PASSLEN - 1.f, PASSLEN + 1.f);
        dots.lastFrame     = std::chrono::system_clock::now();
    }

    const auto  DELTA = std::clamp((int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dots.lastFrame).count(), 0, 20000);

    const float TOADD = DELTA / 1000000.0 * dots.speedPerSecond;

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

    bool forceReload = false;

    updateFade();
    updateDots();
    updateFailTex();
    updateHiddenInputState();

    float  passAlpha = g_pHyprlock->passwordCheckWaiting() ? 0.5 : 1.0;

    CColor outerCol = outer;
    outerCol.a *= fade.a * data.opacity;
    CColor innerCol = inner;
    innerCol.a *= fade.a * data.opacity;
    CColor fontCol = font;
    fontCol.a *= fade.a * data.opacity * passAlpha;

    g_pRenderer->renderRect(outerBox, outerCol, outerBox.h / 2.0);

    const auto PASSLEN = g_pHyprlock->getPasswordBufferLen();

    if (PASSLEN != 0 && hiddenInputState.enabled) {
        CBox     outerBoxScaled = outerBox;
        Vector2D p              = outerBox.pos();
        outerBoxScaled.translate(-p).scale(0.5).translate(p);
        if (hiddenInputState.lastQuadrant > 1)
            outerBoxScaled.y += outerBoxScaled.h;
        if (hiddenInputState.lastQuadrant % 2 == 1)
            outerBoxScaled.x += outerBoxScaled.w;
        glEnable(GL_SCISSOR_TEST);
        glScissor(outerBoxScaled.x, outerBoxScaled.y, outerBoxScaled.w, outerBoxScaled.h);
        g_pRenderer->renderRect(outerBox, hiddenInputState.lastColor, outerBox.h / 2.0);
        glScissor(0, 0, viewport.x, viewport.y);
        glDisable(GL_SCISSOR_TEST);
    }

    g_pRenderer->renderRect(inputFieldBox, innerCol, inputFieldBox.h / 2.0);

    const int PASS_SIZE    = std::nearbyint(inputFieldBox.h * dt_size * 0.5f) * 2.f;
    const int PASS_SPACING = std::floor(PASS_SIZE * dt_space);

    if (!hiddenInputState.enabled) {
        for (size_t i = 0; i < std::floor(dots.currentAmount); ++i) {
            Vector2D currentPos = inputFieldBox.pos() + Vector2D{PASS_SPACING * 2, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} + Vector2D{(PASS_SIZE + PASS_SPACING) * i, 0};
            CBox     box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
            g_pRenderer->renderRect(box, fontCol, PASS_SIZE / 2.0);
        }

        if (dots.currentAmount != std::floor(dots.currentAmount)) {
            Vector2D currentPos = inputFieldBox.pos() + Vector2D{PASS_SPACING * 2, inputFieldBox.h / 2.f - PASS_SIZE / 2.f} +
                Vector2D{(PASS_SIZE + PASS_SPACING) * std::floor(dots.currentAmount), 0};
            CBox box{currentPos, Vector2D{PASS_SIZE, PASS_SIZE}};
            fontCol.a *= (dots.currentAmount - std::floor(dots.currentAmount)) * data.opacity;
            g_pRenderer->renderRect(box, fontCol, PASS_SIZE / 2.0);
        }
    }

    if (PASSLEN == 0 && !placeholder.resourceID.empty()) {
        SPreloadedAsset* currAsset = nullptr;

        if (!placeholder.failID.empty()) {
            if (!placeholder.failAsset)
                placeholder.failAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.failID);

            currAsset = placeholder.failAsset;
        } else {
            if (!placeholder.asset)
                placeholder.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

            currAsset = placeholder.asset;
        }

        if (currAsset) {
            Vector2D pos = outerBox.pos() + outerBox.size() / 2.f;
            pos          = pos - currAsset->texture.m_vSize / 2.f;
            CBox textbox{pos, currAsset->texture.m_vSize};
            g_pRenderer->renderTexture(textbox, currAsset->texture, data.opacity * fade.a, 0);
        } else
            forceReload = true;
    }

    return dots.currentAmount != PASSLEN || data.opacity < 1.0 || fade.a < 1.0 || forceReload;
}

void CPasswordInputField::updateFailTex() {
    const auto FAIL = g_pHyprlock->passwordLastFailReason();

    if (g_pHyprlock->passwordCheckWaiting())
        placeholder.canGetNewFail = true;

    if (g_pHyprlock->getPasswordBufferLen() != 0) {
        if (placeholder.failAsset) {
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.failAsset);
            placeholder.failAsset = nullptr;
            placeholder.failID    = "";
        }
        return;
    }

    if (!FAIL.has_value() || !placeholder.canGetNewFail)
        return;

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = "input-error:" + std::to_string((uintptr_t)this) + ",time:" + std::to_string(time(nullptr));
    placeholder.failID           = request.id;
    request.asset                = "<span style=\"italic\">" + FAIL.value() + "</span>";
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = std::string{"Sans"};
    request.props["color"]       = CColor{1.0 - font.r, 1.0 - font.g, 1.0 - font.b, 0.5};
    request.props["font_size"]   = (int)size.y / 4;
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    placeholder.canGetNewFail = false;
}

void CPasswordInputField::updateHiddenInputState() {
    if (!hiddenInputState.enabled || (size_t)hiddenInputState.lastPasswordLength == g_pHyprlock->getPasswordBufferLen())
        return;

    // randomize new thang
    hiddenInputState.lastPasswordLength = g_pHyprlock->getPasswordBufferLen();

    float r1 = (rand() % 100) / 255.0;
    float r2 = (rand() % 100) / 255.0;
    int   r3 = rand() % 3;
    int   r4 = rand() % 2;
    int   r5 = rand() % 2;

    ((float*)&hiddenInputState.lastColor.r)[r3]            = r1 + 155 / 255.0;
    ((float*)&hiddenInputState.lastColor.r)[(r3 + r4) % 3] = r2 + 155 / 255.0;

    for (int i = 0; i < 3; ++i) {
        if (i != r3 && i != ((r3 + r4) % 3)) {
            ((float*)&hiddenInputState.lastColor.r)[i] = 1.0 - ((float*)&hiddenInputState.lastColor.r)[r5 ? r3 : ((r3 + r4) % 3)];
        }
    }

    hiddenInputState.lastColor.a  = 1.0;
    hiddenInputState.lastQuadrant = (hiddenInputState.lastQuadrant + rand() % 3 + 1) % 4;
}
