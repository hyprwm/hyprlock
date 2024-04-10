#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "src/core/Auth.hpp"
#include <algorithm>

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

CPasswordInputField::CPasswordInputField(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props, const std::string& output) :
    outputStringPort(output), shadow(this, props, viewport_) {
    size                     = std::any_cast<Hyprlang::VEC2>(props.at("size"));
    outThick                 = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
    dots.size                = std::any_cast<Hyprlang::FLOAT>(props.at("dots_size"));
    dots.spacing             = std::any_cast<Hyprlang::FLOAT>(props.at("dots_spacing"));
    dots.center              = std::any_cast<Hyprlang::INT>(props.at("dots_center"));
    dots.rounding            = std::any_cast<Hyprlang::INT>(props.at("dots_rounding"));
    fadeOnEmpty              = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
    fadeTimeoutMs            = std::any_cast<Hyprlang::INT>(props.at("fade_timeout"));
    hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(props.at("hide_input"));
    rounding                 = std::any_cast<Hyprlang::INT>(props.at("rounding"));
    configPlaceholderText    = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));
    configFailText           = std::any_cast<Hyprlang::STRING>(props.at("fail_text"));
    col.transitionMs         = std::any_cast<Hyprlang::INT>(props.at("fail_transition"));
    col.outer                = std::any_cast<Hyprlang::INT>(props.at("outer_color"));
    col.inner                = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
    col.font                 = std::any_cast<Hyprlang::INT>(props.at("font_color"));
    col.fail                 = std::any_cast<Hyprlang::INT>(props.at("fail_color"));
    col.check                = std::any_cast<Hyprlang::INT>(props.at("check_color"));
    col.both                 = std::any_cast<Hyprlang::INT>(props.at("bothlock_color"));
    col.caps                 = std::any_cast<Hyprlang::INT>(props.at("capslock_color"));
    col.num                  = std::any_cast<Hyprlang::INT>(props.at("numlock_color"));
    col.invertNum            = std::any_cast<Hyprlang::INT>(props.at("invert_numlock"));
    col.swapFont             = std::any_cast<Hyprlang::INT>(props.at("swap_font_color"));
    viewport                 = viewport_;

    auto POS__ = std::any_cast<Hyprlang::VEC2>(props.at("position"));
    pos        = {POS__.x, POS__.y};
    configPos  = pos;
    configSize = size;

    halign = std::any_cast<Hyprlang::STRING>(props.at("halign"));
    valign = std::any_cast<Hyprlang::STRING>(props.at("valign"));

    pos              = posFromHVAlign(viewport, size, pos, halign, valign);
    dots.size        = std::clamp(dots.size, 0.2f, 0.8f);
    dots.spacing     = std::clamp(dots.spacing, 0.f, 1.f);
    col.transitionMs = std::clamp(col.transitionMs, 0, 1000);

    col.both = col.both == -1 ? col.outer : col.both;
    col.caps = col.caps == -1 ? col.outer : col.caps;
    col.num  = col.num == -1 ? col.outer : col.num;

    g_pHyprlock->m_bNumLock = col.invertNum;

    // Render placeholder if either placeholder_text or fail_text are non-empty
    // as placeholder must be rendered to show fail_text
    if (!configPlaceholderText.empty() || !configFailText.empty()) {
        placeholder.currentText = configPlaceholderText;

        replaceAll(placeholder.currentText, "$PROMPT", "");

        placeholder.resourceID = "placeholder:" + placeholder.currentText + std::to_string((uintptr_t)this);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = placeholder.resourceID;
        request.asset                = placeholder.currentText;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = std::string{"Sans"};
        request.props["color"]       = CColor{1.0 - col.font.r, 1.0 - col.font.g, 1.0 - col.font.b, 0.5};
        request.props["font_size"]   = (int)size.y / 4;
        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }
}

static void fadeOutCallback(std::shared_ptr<CTimer> self, void* data) {
    CPasswordInputField* p = (CPasswordInputField*)data;

    p->onFadeOutTimer();
}

void CPasswordInputField::onFadeOutTimer() {
    fade.allowFadeOut = true;
    fade.fadeOutTimer.reset();

    g_pHyprlock->renderOutput(outputStringPort);
}

void CPasswordInputField::updateFade() {
    if (!fadeOnEmpty) {
        fade.a = 1.0;
        return;
    }

    const bool INPUTUSED = passwordLength > 0 || checkWaiting;

    if (INPUTUSED && fade.allowFadeOut)
        fade.allowFadeOut = false;

    if (INPUTUSED && fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (!INPUTUSED && fade.a != 0.0 && (!fade.animated || fade.appearing)) {
        if (fade.allowFadeOut || fadeTimeoutMs == 0) {
            fade.a            = 1.0;
            fade.animated     = true;
            fade.appearing    = false;
            fade.start        = std::chrono::system_clock::now();
            fade.allowFadeOut = false;
        } else if (!fade.fadeOutTimer.get())
            fade.fadeOutTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(fadeTimeoutMs), fadeOutCallback, this);
    }

    if (INPUTUSED && fade.a != 1.0 && (!fade.animated || !fade.appearing)) {
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
    checkWaiting   = g_pAuth->checkWaiting();

    updateFade();
    updateDots();
    updatePlaceholder();
    updateColors();
    updateHiddenInputState();

    static auto TIMER = std::chrono::system_clock::now();

    if (placeholder.asset) {
        const auto TARGETSIZEX = placeholder.asset->texture.m_vSize.x + inputFieldBox.h;

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

    CColor outerCol = col.outer;
    outerCol.a *= fade.a * data.opacity;
    CColor innerCol = col.inner;
    innerCol.a *= fade.a * data.opacity;
    CColor fontCol = col.font;
    fontCol.a *= fade.a * data.opacity;

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

        if (!placeholder.asset)
            placeholder.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

        currAsset = placeholder.asset;

        if (currAsset) {
            Vector2D pos = outerBox.pos() + outerBox.size() / 2.f;
            pos          = pos - currAsset->texture.m_vSize / 2.f;
            CBox textbox{pos, currAsset->texture.m_vSize};
            g_pRenderer->renderTexture(textbox, currAsset->texture, data.opacity * fade.a, 0);
        } else
            forceReload = true;
    }

    return dots.currentAmount != passwordLength || fade.animated || col.animated || redrawShadow || data.opacity < 1.0 || forceReload;
}

void CPasswordInputField::updatePlaceholder() {
    if (passwordLength != 0) {
        if (placeholder.asset && /* keep prompt asset cause it is likely to be used again */ placeholder.isFailText) {
            std::erase(placeholder.registeredResourceIDs, placeholder.resourceID);
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);
            placeholder.asset      = nullptr;
            placeholder.resourceID = "";
            redrawShadow           = true;
        }
        return;
    }

    const auto AUTHFEEDBACK = g_pAuth->m_bDisplayFailText ? g_pAuth->getLastFailText().value_or("Ups, no fail text?") : g_pAuth->getLastPrompt().value_or("Ups, no prompt?");

    if (placeholder.lastAuthFeedback == AUTHFEEDBACK && g_pHyprlock->getPasswordFailedAttempts() == placeholder.failedAttempts)
        return;

    placeholder.failedAttempts   = g_pHyprlock->getPasswordFailedAttempts();
    placeholder.isFailText       = g_pAuth->m_bDisplayFailText;
    placeholder.lastAuthFeedback = AUTHFEEDBACK;

    placeholder.asset = nullptr;

    if (placeholder.isFailText) {
        placeholder.currentText = configFailText;
        replaceAll(placeholder.currentText, "$FAIL", AUTHFEEDBACK);
        replaceAll(placeholder.currentText, "$ATTEMPTS", std::to_string(placeholder.failedAttempts));
    } else {
        placeholder.currentText = configPlaceholderText;
        replaceAll(placeholder.currentText, "$PROMPT", AUTHFEEDBACK);
    }

    placeholder.resourceID = "placeholder:" + placeholder.currentText + std::to_string((uintptr_t)this);
    if (std::find(placeholder.registeredResourceIDs.begin(), placeholder.registeredResourceIDs.end(), placeholder.resourceID) != placeholder.registeredResourceIDs.end())
        return;

    placeholder.registeredResourceIDs.push_back(placeholder.resourceID);

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = placeholder.resourceID;
    request.asset                = placeholder.currentText;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = std::string{"Sans"};
    request.props["color"]       = (placeholder.isFailText) ? col.fail : col.font;
    request.props["font_size"]   = (int)size.y / 4;
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
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

static void changeChannel(const float& source, const float& target, float& subject, const double& multi, bool& animated) {

    const float DELTA = target - source;

    if (subject != target) {
        subject += DELTA * multi;
        animated = true;

        if ((source < target && subject > target) || (source > target && subject < target))
            subject = target;
    }
}

static void changeColor(const CColor& source, const CColor& target, CColor& subject, const double& multi, bool& animated) {

    changeChannel(source.r, target.r, subject.r, multi, animated);
    changeChannel(source.g, target.g, subject.g, multi, animated);
    changeChannel(source.b, target.b, subject.b, multi, animated);
    changeChannel(source.a, target.a, subject.a, multi, animated);
}

void CPasswordInputField::updateColors() {
    static auto OUTER = col.outer, TARGET = OUTER, SOURCE = OUTER;
    static auto INNER = col.inner, ITARGET = INNER, ISOURCE = INNER;
    static auto FONT = col.font, FTARGET = FONT, FSOURCE = FONT;

    const bool  BORDERLESS = outThick == 0;

    if (col.animated) {
        // some cases when events happen too quick (within transitionMs)
        // TODO: find more?
        const bool LOCKCHANGED = col.stateNum != (col.invertNum ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock) || col.stateCaps != g_pHyprlock->m_bCapsLock;
        const bool ANIMONCHECK = checkWaiting && (TARGET == (BORDERLESS ? INNER : OUTER) || TARGET == col.fail);

        if (LOCKCHANGED || ANIMONCHECK) {
            const bool EQUALCOLORS = ANIMONCHECK && OUTER == col.check;
            // to avoid throttle when check_color set to the same as outer.
            SOURCE  = BORDERLESS ? (EQUALCOLORS ? INNER : col.inner) : col.outer;
            FSOURCE = EQUALCOLORS ? FONT : col.font;
            ISOURCE = EQUALCOLORS ? INNER : col.inner;
        }
    } else {
        SOURCE  = BORDERLESS ? col.inner : col.outer;
        FSOURCE = col.font;
        ISOURCE = col.inner;
    }

    col.stateNum  = col.invertNum ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock;
    col.stateCaps = g_pHyprlock->m_bCapsLock;

    if (!placeholder.isFailText || passwordLength > 0 || (passwordLength == 0 && checkWaiting)) {
        if (g_pHyprlock->m_bFadeStarted) {
            if (TARGET == col.check)
                SOURCE = BORDERLESS ? col.inner : col.outer;
            col.transitionMs = 100;
            TARGET           = BORDERLESS ? INNER : OUTER;
        } else if (checkWaiting) {
            FTARGET               = col.swapFont ? INNER : FONT;
            const float PASSALPHA = FTARGET.a * 0.5;
            FTARGET.a             = PASSALPHA;

            TARGET  = col.check;
            ITARGET = col.swapFont ? FONT : INNER;
        } else if (col.stateCaps && col.stateNum && col.both != OUTER) {
            TARGET  = col.both;
            FTARGET = col.swapFont && BORDERLESS ? INNER : FONT;
        } else if (col.stateCaps && col.caps != OUTER) {
            TARGET  = col.caps;
            FTARGET = col.swapFont && BORDERLESS ? INNER : FONT;
        } else if (col.stateNum && col.num != OUTER) {
            TARGET  = col.num;
            FTARGET = col.swapFont && BORDERLESS ? INNER : FONT;
        } else {
            // if quickly pressed after failure
            if (col.animated && TARGET == col.fail)
                SOURCE = BORDERLESS ? col.inner : col.outer;

            TARGET  = BORDERLESS ? INNER : OUTER;
            FTARGET = FONT;
            ITARGET = INNER;
        }
    } else {
        FSOURCE               = col.swapFont ? INNER : FONT;
        const float PASSALPHA = FSOURCE.a * 0.5;
        FSOURCE.a             = PASSALPHA;
        FTARGET               = FONT;

        SOURCE  = col.check;
        TARGET  = col.fail;
        ISOURCE = FONT;
        ITARGET = FONT;

        if (fade.animated || fade.a < 1.0) {
            TARGET = BORDERLESS ? INNER : OUTER;
            SOURCE = col.fail;
        }
    }

    col.animated = false;

    const bool SWAPDONE = !BORDERLESS && col.swapFont ? col.inner == ITARGET : true;

    if ((BORDERLESS ? col.inner : col.outer) == TARGET && col.font == FTARGET && SWAPDONE) {
        col.shouldStart = true;
        return;
    }

    if (col.shouldStart) {
        col.lastFrame   = std::chrono::system_clock::now();
        col.shouldStart = false;
    }

    const auto MULTI = col.transitionMs == 0 ?
        1.0 :
        std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - col.lastFrame).count() / (double)col.transitionMs, 0.016, 0.5);

    changeColor(SOURCE, TARGET, (BORDERLESS ? col.inner : col.outer), MULTI, col.animated);
    changeColor(FSOURCE, FTARGET, col.font, MULTI, col.animated);
    if (col.swapFont && !BORDERLESS)
        changeColor(ISOURCE, ITARGET, col.inner, MULTI, col.animated);

    col.lastFrame = std::chrono::system_clock::now();
}
