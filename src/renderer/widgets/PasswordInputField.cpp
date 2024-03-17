#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>

CPasswordInputField::CPasswordInputField(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props) : shadow(this, props, viewport_) {
    size                     = std::any_cast<Hyprlang::VEC2>(props.at("size"));
    inner                    = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
    outThick                 = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
    dots.size                = std::any_cast<Hyprlang::FLOAT>(props.at("dots_size"));
    dots.spacing             = std::any_cast<Hyprlang::FLOAT>(props.at("dots_spacing"));
    dots.center              = std::any_cast<Hyprlang::INT>(props.at("dots_center"));
    dots.rounding            = std::any_cast<Hyprlang::INT>(props.at("dots_rounding"));
    fadeOnEmpty              = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
    fadeTimeoutMs            = std::any_cast<Hyprlang::INT>(props.at("fade_timeout"));
    font                     = std::any_cast<Hyprlang::INT>(props.at("font_color"));
    hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(props.at("hide_input"));
    rounding                 = std::any_cast<Hyprlang::INT>(props.at("rounding"));
    configFailText           = std::any_cast<Hyprlang::STRING>(props.at("fail_text"));
    outerColor.transitionMs  = std::any_cast<Hyprlang::INT>(props.at("fail_transition"));
    outerColor.main          = std::any_cast<Hyprlang::INT>(props.at("outer_color"));
    outerColor.fail          = std::any_cast<Hyprlang::INT>(props.at("fail_color"));
    outerColor.check         = std::any_cast<Hyprlang::INT>(props.at("check_color"));
    outerColor.both          = std::any_cast<Hyprlang::INT>(props.at("bothlock_color"));
    outerColor.caps          = std::any_cast<Hyprlang::INT>(props.at("capslock_color"));
    outerColor.num           = std::any_cast<Hyprlang::INT>(props.at("numlock_color"));
    outerColor.invertNum     = std::any_cast<Hyprlang::INT>(props.at("invert_numlock"));
    viewport                 = viewport_;

    auto POS__ = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    pos        = {POS__.x, POS__.y};
    configPos  = pos;
    configSize = size;

    halign = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign = std::any_cast<Hyprlang::STRING>(props.at("valign"));

    pos                     = posFromHVAlign(viewport, size, pos, halign, valign);
    dots.size               = std::clamp(dots.size, 0.2f, 0.8f);
    dots.spacing            = std::clamp(dots.spacing, 0.f, 1.f);
    outerColor.transitionMs = std::clamp(outerColor.transitionMs, 0, 1000);

    outerColor.both = outerColor.both == -1 ? outerColor.main : outerColor.both;
    outerColor.caps = outerColor.caps == -1 ? outerColor.main : outerColor.caps;
    outerColor.num  = outerColor.num == -1 ? outerColor.main : outerColor.num;

    g_pHyprlock->m_bNumLock = outerColor.invertNum;

    std::string placeholderText = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));

    // Render placeholder if either placeholder_text or fail_text are non-empty
    // as placeholder must be rendered to show fail_text
    if (!placeholderText.empty() || !configFailText.empty()) {
        placeholder.resourceID = "placeholder:" + std::to_string((uintptr_t)this);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = placeholder.resourceID;
        request.asset                = placeholderText;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = std::string{"Sans"};
        request.props["color"]       = CColor{1.0 - font.r, 1.0 - font.g, 1.0 - font.b, 0.5};
        request.props["font_size"]   = (int)size.y / 4;
        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }
}

static void replaceAllFail(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

static void fadeOutCallback(std::shared_ptr<CTimer> self, void* data) {
    CPasswordInputField* p = (CPasswordInputField*)data;

    p->onFadeOutTimer();

    for (auto& o : g_pHyprlock->m_vOutputs) {
        o->sessionLockSurface->render();
    }
}

void CPasswordInputField::onFadeOutTimer() {
    fade.allowFadeOut = true;
    fade.fadeOutTimer.reset();
}

void CPasswordInputField::updateFade() {
    if (!fadeOnEmpty) {
        fade.a = 1.0;
        return;
    }

    if (passwordLength > 0 && fade.allowFadeOut)
        fade.allowFadeOut = false;

    if (passwordLength > 0 && fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (passwordLength == 0 && fade.a != 0.0 && (!fade.animated || fade.appearing)) {
        if (fade.allowFadeOut || fadeTimeoutMs == 0) {
            fade.a            = 1.0;
            fade.animated     = true;
            fade.appearing    = false;
            fade.start        = std::chrono::system_clock::now();
            fade.allowFadeOut = false;
        } else if (!fade.fadeOutTimer.get())
            fade.fadeOutTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(fadeTimeoutMs), fadeOutCallback, this);
    }

    if (passwordLength > 0 && fade.a != 1.0 && (!fade.animated || !fade.appearing)) {
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

        redrawShadow = true;
    }
}

void CPasswordInputField::updateDots() {
    if (passwordLength == dots.currentAmount)
        return;

    if (std::abs(passwordLength - dots.currentAmount) > 1) {
        dots.currentAmount = std::clamp(dots.currentAmount, passwordLength - 1.f, passwordLength + 1.f);
        dots.lastFrame     = std::chrono::system_clock::now();
    }

    const auto  DELTA = std::clamp((int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dots.lastFrame).count(), 0, 20000);

    const float TOADD = DELTA / 1000000.0 * dots.speedPerSecond;

    if (passwordLength > dots.currentAmount) {
        dots.currentAmount += TOADD;
        if (dots.currentAmount > passwordLength)
            dots.currentAmount = passwordLength;
    } else if (passwordLength < dots.currentAmount) {
        dots.currentAmount -= TOADD;
        if (dots.currentAmount < passwordLength)
            dots.currentAmount = passwordLength;
    }

    dots.lastFrame = std::chrono::system_clock::now();
}

bool CPasswordInputField::draw(const SRenderData& data) {
    CBox inputFieldBox = {pos, size};
    CBox outerBox      = {pos - Vector2D{outThick, outThick}, size + Vector2D{outThick * 2, outThick * 2}};

    if (firstRender || redrawShadow) {
        firstRender  = false;
        redrawShadow = false;
        shadow.markShadowDirty();
    }

    bool forceReload = false;

    passwordLength = g_pHyprlock->getPasswordBufferDisplayLen();
    checkWaiting   = g_pHyprlock->passwordCheckWaiting();

    updateFade();
    updateDots();
    updateFailTex();
    updateOuter();
    updateHiddenInputState();

    static auto TIMER = std::chrono::system_clock::now();

    if (placeholder.failAsset) {
        const auto TARGETSIZEX = placeholder.failAsset->texture.m_vSize.x + inputFieldBox.h;

        if (size.x < TARGETSIZEX) {
            const auto DELTA = std::clamp((int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - TIMER).count(), 8000, 20000);
            TIMER            = std::chrono::system_clock::now();
            forceReload      = true;

            size.x += std::clamp((TARGETSIZEX - size.x) * DELTA / 100000.0, 1.0, 1000.0);

            if (size.x > TARGETSIZEX) {
                size.x       = TARGETSIZEX;
                redrawShadow = true;
            }
        }

        pos = posFromHVAlign(viewport, size, configPos, halign, valign);
    } else if (size.x != configSize.x) {
        size.x = configSize.x;
        pos    = posFromHVAlign(viewport, size, configPos, halign, valign);
    }

    SRenderData shadowData = data;
    shadowData.opacity *= fade.a;
    shadow.draw(shadowData);

    float  passAlpha = checkWaiting ? 0.5 : 1.0;

    CColor outerCol = outerColor.main;
    outerCol.a *= fade.a * data.opacity;
    CColor innerCol = inner;
    innerCol.a *= fade.a * data.opacity;
    CColor fontCol = font;
    fontCol.a *= fade.a * data.opacity * passAlpha;

    if (outThick > 0) {
        g_pRenderer->renderRect(outerBox, outerCol, rounding == -1 ? outerBox.h / 2.0 : rounding);

        if (passwordLength != 0 && hiddenInputState.enabled && !fade.animated && data.opacity == 1.0) {
            CBox     outerBoxScaled = outerBox;
            Vector2D p              = outerBox.pos();
            outerBoxScaled.translate(-p).scale(0.5).translate(p);
            if (hiddenInputState.lastQuadrant > 1)
                outerBoxScaled.y += outerBoxScaled.h;
            if (hiddenInputState.lastQuadrant % 2 == 1)
                outerBoxScaled.x += outerBoxScaled.w;
            glEnable(GL_SCISSOR_TEST);
            glScissor(outerBoxScaled.x, outerBoxScaled.y, outerBoxScaled.w, outerBoxScaled.h);
            g_pRenderer->renderRect(outerBox, hiddenInputState.lastColor, rounding == -1 ? outerBox.h / 2.0 : rounding);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    g_pRenderer->renderRect(inputFieldBox, innerCol, rounding == -1 ? inputFieldBox.h / 2.0 : rounding - outThick);

    if (!hiddenInputState.enabled && !g_pHyprlock->m_bFadeStarted) {
        const int   PASS_SIZE      = std::nearbyint(inputFieldBox.h * dots.size * 0.5f) * 2.f;
        const int   PASS_SPACING   = std::floor(PASS_SIZE * dots.spacing);
        const int   DOT_PAD        = (inputFieldBox.h - PASS_SIZE) / 2;
        const int   DOT_AREA_WIDTH = inputFieldBox.w - DOT_PAD * 2;                                 // avail width for dots
        const int   MAX_DOTS       = std::round(DOT_AREA_WIDTH * 1.0 / (PASS_SIZE + PASS_SPACING)); // max amount of dots that can fit in the area
        const int   DOT_FLOORED    = std::floor(dots.currentAmount);
        const float DOT_ALPHA      = fontCol.a;

        // Calculate the total width required for all dots including spaces between them
        const int TOTAL_DOTS_WIDTH = (PASS_SIZE + PASS_SPACING) * dots.currentAmount - PASS_SPACING;

        // Calculate starting x-position to ensure dots stay centered within the input field
        int xstart = dots.center ? (DOT_AREA_WIDTH - TOTAL_DOTS_WIDTH) / 2 + DOT_PAD : DOT_PAD;

        if (dots.currentAmount > MAX_DOTS)
            xstart = (inputFieldBox.w + MAX_DOTS * (PASS_SIZE + PASS_SPACING) - PASS_SPACING - 2 * TOTAL_DOTS_WIDTH) / 2;

        if (dots.rounding == -1)
            dots.rounding = PASS_SIZE / 2.0;
        else if (dots.rounding == -2)
            dots.rounding = rounding == -1 ? PASS_SIZE / 2.0 : rounding * dots.size;

        for (int i = 0; i < dots.currentAmount; ++i) {
            if (i < DOT_FLOORED - MAX_DOTS)
                continue;

            if (dots.currentAmount != DOT_FLOORED) {
                if (i == DOT_FLOORED)
                    fontCol.a *= (dots.currentAmount - DOT_FLOORED) * data.opacity;
                else if (i == DOT_FLOORED - MAX_DOTS)
                    fontCol.a *= (1 - dots.currentAmount + DOT_FLOORED) * data.opacity;
            }

            Vector2D dotPosition =
                inputFieldBox.pos() + Vector2D{xstart + (int)inputFieldBox.w % 2 / 2.f + i * (PASS_SIZE + PASS_SPACING), inputFieldBox.h / 2.f - PASS_SIZE / 2.f};
            CBox box{dotPosition, Vector2D{PASS_SIZE, PASS_SIZE}};
            g_pRenderer->renderRect(box, fontCol, dots.rounding);
            fontCol.a = DOT_ALPHA;
        }
    }

    if (passwordLength == 0 && !placeholder.resourceID.empty()) {
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

    return dots.currentAmount != passwordLength || fade.animated || outerColor.animated || redrawShadow || data.opacity < 1.0 || forceReload;
}

void CPasswordInputField::updateFailTex() {
    const auto FAIL = g_pHyprlock->passwordLastFailReason();

    if (checkWaiting)
        placeholder.canGetNewFail = true;

    if (passwordLength != 0 || (checkWaiting && passwordLength == 0)) {
        if (placeholder.failAsset) {
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.failAsset);
            placeholder.failAsset = nullptr;
            placeholder.failID    = "";
            redrawShadow          = true;
        }
        return;
    }

    if (!FAIL.has_value() || !placeholder.canGetNewFail)
        return;

    placeholder.failText = configFailText;
    replaceAllFail(placeholder.failText, "$FAIL", FAIL.value());
    replaceAllFail(placeholder.failText, "$ATTEMPTS", std::to_string(g_pHyprlock->getPasswordFailedAttempts()));

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = "input-error:" + std::to_string((uintptr_t)this) + ",time:" + std::to_string(time(nullptr));
    placeholder.failID           = request.id;
    request.asset                = placeholder.failText;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = std::string{"Sans"};
    request.props["color"]       = outerColor.fail;
    request.props["font_size"]   = (int)size.y / 4;
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    placeholder.canGetNewFail = false;
}

void CPasswordInputField::updateHiddenInputState() {
    if (!hiddenInputState.enabled || (size_t)hiddenInputState.lastPasswordLength == passwordLength)
        return;

    // randomize new thang
    hiddenInputState.lastPasswordLength = passwordLength;

    srand(std::chrono::system_clock::now().time_since_epoch().count());
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

void CPasswordInputField::updateOuter() {
    if (outThick == 0)
        return;

    static auto OUTER = outerColor.main, TARGET = OUTER, SOURCE = OUTER;
    static auto TIMER = std::chrono::system_clock::now();

    if (outerColor.animated) {
        if (outerColor.stateNum != (outerColor.invertNum ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock) || outerColor.stateCaps != g_pHyprlock->m_bCapsLock)
            SOURCE = outerColor.main;
    } else
        SOURCE = outerColor.main;

    outerColor.animated  = false;
    outerColor.stateNum  = outerColor.invertNum ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock;
    outerColor.stateCaps = g_pHyprlock->m_bCapsLock;

    if (placeholder.failID.empty()) {
        if (g_pHyprlock->m_bFadeStarted) {
            if (TARGET == outerColor.check)
                SOURCE = outerColor.main;
            outerColor.transitionMs = 100;
            TARGET                  = OUTER;
        } else if (checkWaiting)
            TARGET = outerColor.check;
        else if (outerColor.both != OUTER && outerColor.stateCaps && outerColor.stateNum)
            TARGET = outerColor.both;
        else if (outerColor.caps != OUTER && outerColor.stateCaps)
            TARGET = outerColor.caps;
        else if (outerColor.num != OUTER && outerColor.stateNum)
            TARGET = outerColor.num;
        else
            TARGET = OUTER;
    } else {
        SOURCE = outerColor.check;
        TARGET = outerColor.fail;

        if (fade.animated || fade.a < 1.0) {
            TARGET = OUTER;
            SOURCE = outerColor.fail;
        }
    }

    if (outerColor.main == TARGET)
        return;

    if (outerColor.main == SOURCE && !fade.animated)
        TIMER = std::chrono::system_clock::now();

    const auto MULTI = outerColor.transitionMs == 0 ?
        1.0 :
        std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - TIMER).count() / (double)outerColor.transitionMs, 0.02, 0.5);
    const auto DELTA = TARGET - SOURCE;

    if (outerColor.main.r != TARGET.r) {
        outerColor.main.r += DELTA.r * MULTI;
        outerColor.animated = true;

        if ((SOURCE.r < TARGET.r && outerColor.main.r > TARGET.r) || (SOURCE.r > TARGET.r && outerColor.main.r < TARGET.r))
            outerColor.main.r = TARGET.r;
    }

    if (outerColor.main.g != TARGET.g) {
        outerColor.main.g += DELTA.g * MULTI;
        outerColor.animated = true;

        if ((SOURCE.g < TARGET.g && outerColor.main.g > TARGET.g) || (SOURCE.g > TARGET.g && outerColor.main.g < TARGET.g))
            outerColor.main.g = TARGET.g;
    }

    if (outerColor.main.b != TARGET.b) {
        outerColor.main.b += DELTA.b * MULTI;
        outerColor.animated = true;

        if ((SOURCE.b < TARGET.b && outerColor.main.b > TARGET.b) || (SOURCE.b > TARGET.b && outerColor.main.b < TARGET.b))
            outerColor.main.b = TARGET.b;
    }

    if (outerColor.main.a != TARGET.a) {
        outerColor.main.a += DELTA.a * MULTI;
        outerColor.animated = true;

        if ((SOURCE.a < TARGET.a && outerColor.main.a > TARGET.a) || (SOURCE.a > TARGET.a && outerColor.main.a < TARGET.a))
            outerColor.main.a = TARGET.a;
    }

    TIMER = std::chrono::system_clock::now();
}
