#include "PasswordInputField.hpp"
#include "icons/eye-open.hpp"
#include "icons/eye-closed.hpp"
#include "../Renderer.hpp"
#include "../AsyncResourceGatherer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../auth/Auth.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../helpers/Color.hpp"
#include <functional>
#include <cmath>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/string/String.hpp>
#include <algorithm>
#include <hyprlang.hpp>

using namespace Hyprutils::String;

CPasswordInputField::~CPasswordInputField() {
    reset();
}

void CPasswordInputField::registerSelf(const ASP<CPasswordInputField>& self) {
    m_self = self;

    type = "password-input";
}

void CPasswordInputField::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    reset();

    outputStringPort = pOutput->stringPort;
    viewport         = pOutput->getViewport();

    shadow.configure(m_self, props, viewport);

    try {
        pos                      = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport);
        configSize               = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(viewport);
        halign                   = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign                   = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        outThick                 = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
        password.dots.spacing    = std::any_cast<Hyprlang::FLOAT>(props.at("password-appearance:dots_spacing"));
        password.dots.rounding   = std::any_cast<Hyprlang::INT>(props.at("password-appearance:dots_rounding"));
        password.dots.format     = std::any_cast<Hyprlang::STRING>(props.at("password-appearance:dots_format"));
        password.size            = std::any_cast<Hyprlang::FLOAT>(props.at("password-appearance:size"));
        password.center          = std::any_cast<Hyprlang::INT>(props.at("password-appearance:center"));
        password.allowToggle     = std::any_cast<Hyprlang::INT>(props.at("password-appearance:toggle_password_visibility"));
        password.eye.hide        = std::any_cast<Hyprlang::INT>(props.at("password-appearance:hide_eye"));
        password.eye.margin      = std::any_cast<Hyprlang::INT>(props.at("password-appearance:eye_margin"));
        password.eye.size        = std::any_cast<Hyprlang::FLOAT>(props.at("password-appearance:eye_size"));
        password.eye.placement   = std::any_cast<Hyprlang::STRING>(props.at("password-appearance:eye_placement"));
        fadeOnEmpty              = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
        fadeTimeoutMs            = std::any_cast<Hyprlang::INT>(props.at("fade_timeout"));
        hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(props.at("hide_input"));
        rounding                 = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        configPlaceholderText    = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));
        configFailText           = std::any_cast<Hyprlang::STRING>(props.at("fail_text"));
        fontFamily               = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
        colorConfig.outer        = CGradientValueData::fromAnyPv(props.at("outer_color"));
        colorConfig.inner        = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
        colorConfig.font         = std::any_cast<Hyprlang::INT>(props.at("font_color"));
        colorConfig.fail         = CGradientValueData::fromAnyPv(props.at("fail_color"));
        colorConfig.check        = CGradientValueData::fromAnyPv(props.at("check_color"));
        colorConfig.both         = CGradientValueData::fromAnyPv(props.at("bothlock_color"));
        colorConfig.caps         = CGradientValueData::fromAnyPv(props.at("capslock_color"));
        colorConfig.num          = CGradientValueData::fromAnyPv(props.at("numlock_color"));
        colorConfig.invertNum    = std::any_cast<Hyprlang::INT>(props.at("invert_numlock"));
        colorConfig.swapFont     = std::any_cast<Hyprlang::INT>(props.at("swap_font_color"));
        colorConfig.hiddenBase   = std::any_cast<Hyprlang::INT>(props.at("hide_input_base_color"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CPasswordInputField: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CPasswordInputField: {}", e.what()); //
    }

    configPos       = pos;
    colorState.font = colorConfig.font;

    pos                   = posFromHVAlign(viewport, configSize, pos, halign, valign);
    password.size         = std::clamp(password.size, 0.2f, 0.8f);
    password.dots.spacing = std::clamp(password.dots.spacing, -1.f, 1.f);

    colorConfig.caps = colorConfig.caps->m_bIsFallback ? colorConfig.fail : colorConfig.caps;

    g_pAnimationManager->createAnimation(0.f, fade.a, g_pConfigManager->m_AnimationTree.getConfig("inputFieldFade"));
    g_pAnimationManager->createAnimation(0.f, password.dots.currentAmount, g_pConfigManager->m_AnimationTree.getConfig("inputFieldDots"));
    g_pAnimationManager->createAnimation(configSize, size, g_pConfigManager->m_AnimationTree.getConfig("inputFieldWidth"));
    g_pAnimationManager->createAnimation(colorConfig.inner, colorState.inner, g_pConfigManager->m_AnimationTree.getConfig("inputFieldColors"));
    g_pAnimationManager->createAnimation(*colorConfig.outer, colorState.outer, g_pConfigManager->m_AnimationTree.getConfig("inputFieldColors"));

    srand(std::chrono::system_clock::now().time_since_epoch().count());

    pos = posFromHVAlign(viewport, size->goal(), configPos, halign, valign);

    if (!password.dots.format.empty()) {
        password.dots.resourceID = std::format("input:{}-{}", (uintptr_t)this, password.dots.format);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = password.dots.resourceID;
        request.asset                = password.dots.format;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = fontFamily;
        request.props["color"]       = colorConfig.font;
        request.props["font_size"]   = (int)(std::nearbyint(configSize.y * password.size * 0.5f) * 2.f);

        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }

    // request the inital placeholder asset
    updatePlaceholder();

    if (password.allowToggle)
        updateEye();
}

void CPasswordInputField::reset() {
    if (fade.fadeOutTimer) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (g_pHyprlock->m_bTerminate)
        return;

    if (placeholder.asset)
        g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);

    placeholder.asset = nullptr;
    placeholder.resourceID.clear();
    placeholder.currentText.clear();
}

static void fadeOutCallback(AWP<CPasswordInputField> ref) {
    if (const auto PP = ref.lock(); PP)
        PP->onFadeOutTimer();
}

static void assetReadyCallback(AWP<CPasswordInputField> ref) {
    if (auto PINPUT = ref.lock(); PINPUT)
        PINPUT->renderPasswordUpdate();
}

void CPasswordInputField::onFadeOutTimer() {
    fade.allowFadeOut = true;
    fade.fadeOutTimer.reset();

    g_pHyprlock->renderOutput(outputStringPort);
}

void CPasswordInputField::updateFade() {
    if (!fadeOnEmpty) {
        fade.a->setValueAndWarp(1.0);
        return;
    }

    const bool INPUTUSED = passwordLength > 0 || checkWaiting;

    if (INPUTUSED && fade.allowFadeOut)
        fade.allowFadeOut = false;

    if (INPUTUSED && fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (!INPUTUSED && fade.a->goal() != 0.0) {
        if (fade.allowFadeOut || fadeTimeoutMs == 0) {
            *fade.a           = 0.0;
            fade.allowFadeOut = false;
        } else if (!fade.fadeOutTimer)
            fade.fadeOutTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(fadeTimeoutMs), [REF = m_self](auto, auto) { fadeOutCallback(REF); }, nullptr);

    } else if (INPUTUSED && fade.a->goal() != 1.0)
        *fade.a = 1.0;

    if (fade.a->isBeingAnimated())
        redrawShadow = true;
}

void CPasswordInputField::updateDots() {
    if (password.dots.currentAmount->goal() == passwordLength)
        return;

    if (checkWaiting)
        return;

    if (passwordLength == 0)
        password.dots.currentAmount->setValueAndWarp(passwordLength);
    else
        *password.dots.currentAmount = passwordLength;
}

void CPasswordInputField::updatePassword() {
    std::string& passwordContent = g_pHyprlock->getPasswordBuffer();
    std::string  textResourceID  = std::format("password:{}-{}", (uintptr_t)this, std::hash<std::string>{}(passwordContent));

    if (passwordContent == password.text.content || checkWaiting || g_pRenderer->asyncResourceGatherer->getAssetByID(textResourceID)) {
        return;
    }

    password.text.content = passwordContent;

    CAsyncResourceGatherer::SPreloadRequest request;

    request.id                   = textResourceID;
    request.asset                = password.text.content;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = colorConfig.font;
    request.props["font_size"]   = (int)(std::nearbyint(configSize.y * password.size * 0.5f) * 2.f);
    request.callback             = [REF = m_self]() { assetReadyCallback(REF); };

    password.text.pendingResourceID = textResourceID;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CPasswordInputField::renderPasswordUpdate() {
    auto newAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(password.text.pendingResourceID);
    if (newAsset) {
        // new asset is ready :D
        g_pRenderer->asyncResourceGatherer->unloadAsset(password.text.asset);
        password.text.asset             = newAsset;
        password.text.resourceID        = password.text.pendingResourceID;
        password.text.pendingResourceID = "";
    } else {
        Debug::log(WARN, "Asset {} not available after the asyncResourceGatherer's callback!", password.text.pendingResourceID);

        g_pHyprlock->addTimer(std::chrono::milliseconds(10), [REF = m_self](auto, auto) { assetReadyCallback(REF); }, nullptr);
        return;
    }

    g_pHyprlock->renderOutput(outputStringPort);
}

void CPasswordInputField::updateEye() {
    CAsyncResourceGatherer::SPreloadRequest request;

    password.eye.openRescourceID = std::format("eye-open:{}", (uintptr_t)this);
    request.id                   = password.eye.openRescourceID;
    request.image_buffer         = eye_open_png;
    request.image_size           = eye_open_png_len;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_EMBEDDED_IMAGE;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);

    password.eye.closedRescourceID = std::format("eye-closed:{}", (uintptr_t)this);
    request.id                     = password.eye.closedRescourceID;
    request.image_buffer           = eye_closed_png;
    request.image_size             = eye_closed_png_len;
    request.type                   = CAsyncResourceGatherer::eTargetType::TARGET_EMBEDDED_IMAGE;

    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

bool CPasswordInputField::draw(const SRenderData& data) {
    if (firstRender || redrawShadow) {
        firstRender  = false;
        redrawShadow = false;
        shadow.markShadowDirty();
    }

    bool forceReload = false;

    if (passwordLength != g_pHyprlock->getPasswordBufferDisplayLen() && password.show) {
        g_pRenderer->asyncResourceGatherer->unloadAsset(password.text.asset);
        password.show = false;
    }

    passwordLength = g_pHyprlock->getPasswordBufferDisplayLen();
    checkWaiting   = g_pAuth->checkWaiting();
    displayFail    = g_pAuth->m_bDisplayFailText;

    updateFade();
    updateDots();
    updateColors();
    updatePlaceholder();
    updateWidth();
    updateHiddenInputState();

    CBox        inputFieldBox = {pos, size->value()};
    CBox        outerBox      = {pos - Vector2D{outThick, outThick}, size->value() + Vector2D{outThick * 2, outThick * 2}};

    SRenderData shadowData = data;
    shadowData.opacity *= fade.a->value();

    if (!size->isBeingAnimated())
        shadow.draw(shadowData);

    //CGradientValueData outerGrad = colorState.outer->value();
    //for (auto& c : outerGrad.m_vColors)
    //    c.a *= fade.a->value() * data.opacity;

    CHyprColor innerCol = colorState.inner->value();
    innerCol.a *= fade.a->value() * data.opacity;
    CHyprColor fontCol = colorState.font;
    fontCol.a *= fade.a->value() * data.opacity;

    if (outThick > 0) {
        const auto OUTERROUND = roundingForBorderBox(outerBox, rounding, outThick);
        g_pRenderer->renderBorder(outerBox, colorState.outer->value(), outThick, OUTERROUND, fade.a->value() * data.opacity);

        if (passwordLength != 0 && !checkWaiting && hiddenInputState.enabled) {
            CBox     outerBoxScaled = outerBox;
            Vector2D p              = outerBox.pos();
            outerBoxScaled.translate(-p).scale(0.5).translate(p);
            if (hiddenInputState.lastQuadrant > 1)
                outerBoxScaled.y += outerBoxScaled.h;
            if (hiddenInputState.lastQuadrant % 2 == 1)
                outerBoxScaled.x += outerBoxScaled.w;
            glEnable(GL_SCISSOR_TEST);
            glScissor(outerBoxScaled.x, outerBoxScaled.y, outerBoxScaled.w, outerBoxScaled.h);
            g_pRenderer->renderBorder(outerBox, hiddenInputState.lastColor, outThick, OUTERROUND, fade.a->value() * data.opacity);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    const int ROUND = roundingForBox(inputFieldBox, rounding);
    g_pRenderer->renderRect(inputFieldBox, innerCol, ROUND);

    if (!hiddenInputState.enabled) {
        if (!password.eye.openAsset)
            password.eye.openAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(password.eye.openRescourceID);
        if (!password.eye.closedAsset)
            password.eye.closedAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(password.eye.closedRescourceID);

        int    eyeOffset = 0;
        auto   eyeAsset  = password.show ? password.eye.closedAsset : password.eye.openAsset;
        double eyeHeight = (int)(std::nearbyint(configSize.y * password.eye.size * 0.5f) * 2.f);
        auto   eyeSize   = Vector2D{eyeHeight, eyeHeight};
        if (password.allowToggle && !password.eye.hide) {
            eyeOffset = eyeSize.x + password.eye.margin;
        }

        if (!password.show || !password.allowToggle) {
            const int RECTPASSSIZE = std::nearbyint(inputFieldBox.h * password.size * 0.5f) * 2.f;
            Vector2D  passSize{RECTPASSSIZE, RECTPASSSIZE};
            int       passSpacing = std::floor(passSize.x * password.dots.spacing);

            if (!password.dots.format.empty()) {
                if (!password.dots.asset)
                    password.dots.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(password.dots.resourceID);

                if (!password.dots.asset)
                    forceReload = true;
                else {
                    passSize    = password.dots.asset->texture.m_vSize;
                    passSpacing = std::floor(passSize.x * password.dots.spacing);
                }
            }

            const auto   CURRDOTS     = password.dots.currentAmount->value();
            const double DOTPAD       = (inputFieldBox.h - passSize.y) / 2.0;
            const double DOTAREAWIDTH = inputFieldBox.w - (DOTPAD * 2) - eyeOffset;
            const int    MAXDOTS      = std::round(DOTAREAWIDTH * 1.0 / (passSize.x + passSpacing));
            const int    DOTFLOORED   = std::floor(CURRDOTS);
            const auto   DOTALPHA     = fontCol.a;

            // Calculate the total width required for all dots including spaces between them
            const double CURRWIDTH = ((passSize.x + passSpacing) * CURRDOTS) - passSpacing;

            // Calculate starting x-position to ensure dots stay centered within the input field
            double xstart = password.center ? ((DOTAREAWIDTH - CURRWIDTH) / 2.0) + DOTPAD : DOTPAD;

            if (CURRDOTS > MAXDOTS)
                xstart = (inputFieldBox.w + MAXDOTS * (passSize.x + passSpacing) - passSpacing - 2 * CURRWIDTH - eyeOffset) / 2.0;

            if (password.eye.placement == "left" && password.allowToggle) {
                xstart += eyeOffset;
            }

            if (password.dots.rounding == -1)
                password.dots.rounding = passSize.x / 2.0;
            else if (password.dots.rounding == -2)
                password.dots.rounding = rounding == -1 ? passSize.x / 2.0 : rounding * password.size;

            for (int i = 0; i < CURRDOTS; ++i) {
                if (i < DOTFLOORED - MAXDOTS)
                    continue;

                if (CURRDOTS != DOTFLOORED) {
                    if (i == DOTFLOORED)
                        fontCol.a *= (CURRDOTS - DOTFLOORED) * data.opacity;
                    else if (i == DOTFLOORED - MAXDOTS)
                        fontCol.a *= (1 - CURRDOTS + DOTFLOORED) * data.opacity;
                }

                Vector2D dotPosition = inputFieldBox.pos() + Vector2D{xstart + (i * (passSize.x + passSpacing)), (inputFieldBox.h / 2.0) - (passSize.y / 2.0)};
                CBox     box{dotPosition, passSize};
                if (!password.dots.format.empty()) {
                    if (!password.dots.asset) {
                        fontCol.a = DOTALPHA;
                        break;
                    }

                    g_pRenderer->renderTexture(box, password.dots.asset->texture, fontCol.a, password.dots.rounding);
                } else
                    g_pRenderer->renderRect(box, fontCol, password.dots.rounding);

                fontCol.a = DOTALPHA;
            }
        } else if (password.allowToggle) {
            if (!password.text.asset)
                password.text.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(password.text.resourceID);

            if (password.text.asset) {
                Vector2D passSize  = password.text.asset->texture.m_vSize;
                double   padding   = (inputFieldBox.h - passSize.y) / 2.0;
                double   areaWidth = inputFieldBox.w - (padding * 2) - eyeOffset;

                double   xstart = password.center ? (inputFieldBox.w - passSize.x - eyeOffset) / 2.0 : padding;
                if (passSize.x > areaWidth) {
                    xstart -= (passSize.x - areaWidth) / 2.0;
                }
                if (password.eye.placement == "left") {
                    xstart += eyeOffset;
                }

                Vector2D passwordPosition = inputFieldBox.pos() + Vector2D{xstart, padding};
                CBox     box{passwordPosition, passSize};

                glEnable(GL_SCISSOR_TEST);
                glScissor(inputFieldBox.x + padding + (password.eye.placement == "left" ? eyeOffset : 0), inputFieldBox.y, areaWidth, inputFieldBox.h);
                g_pRenderer->renderTexture(box, password.text.asset->texture, fontCol.a);
                glScissor(0, 0, viewport.x, viewport.y);
                glDisable(GL_SCISSOR_TEST);
            } else {
                forceReload = true;
            }
        }

        if (password.allowToggle && !password.eye.hide && (passwordLength != 0 || checkWaiting)) {
            auto padding     = (inputFieldBox.h - eyeSize.y) / 2.0;
            auto eyePosition = inputFieldBox.pos() + (password.eye.placement == "right" ? Vector2D{inputFieldBox.w - eyeSize.x - padding, padding} : Vector2D{padding, padding});
            CBox box         = {eyePosition, eyeSize};
            g_pRenderer->renderTexture(box, eyeAsset->texture, fontCol.a);
        }
    }

    if (passwordLength == 0 && !checkWaiting && !placeholder.resourceID.empty()) {
        SPreloadedAsset* currAsset = nullptr;

        if (!placeholder.asset)
            placeholder.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

        currAsset = placeholder.asset;

        if (currAsset) {
            const Vector2D ASSETPOS = inputFieldBox.pos() + inputFieldBox.size() / 2.0 - currAsset->texture.m_vSize / 2.0;
            const CBox     ASSETBOX{ASSETPOS, currAsset->texture.m_vSize};

            // Cut the texture to the width of the input field
            glEnable(GL_SCISSOR_TEST);
            glScissor(inputFieldBox.x, inputFieldBox.y, inputFieldBox.w, inputFieldBox.h);
            g_pRenderer->renderTexture(ASSETBOX, currAsset->texture, data.opacity * fade.a->value(), 0);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        } else
            forceReload = true;
    }

    return redrawShadow || forceReload;
}

void CPasswordInputField::updatePlaceholder() {
    if (passwordLength != 0) {
        if (placeholder.asset && /* keep prompt asset cause it is likely to be used again */ displayFail) {
            std::erase(placeholder.registeredResourceIDs, placeholder.resourceID);
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);
            placeholder.asset      = nullptr;
            placeholder.resourceID = "";
            redrawShadow           = true;
        }
        return;
    }

    // already requested a placeholder for the current fail
    if (displayFail && placeholder.failedAttempts == g_pAuth->getFailedAttempts())
        return;

    placeholder.failedAttempts = g_pAuth->getFailedAttempts();

    std::string newText = (displayFail) ? formatString(configFailText).formatted : formatString(configPlaceholderText).formatted;

    // if the text is unchanged we don't need to do anything, unless we are swapping font color
    const auto ALLOWCOLORSWAP = outThick == 0 && colorConfig.swapFont;
    if (!ALLOWCOLORSWAP && newText == placeholder.currentText)
        return;

    const auto NEWRESOURCEID =
        std::format("placeholder:{}{}{}{}{}{}", placeholder.currentText, (uintptr_t)this, colorState.font.r, colorState.font.g, colorState.font.b, colorState.font.a);

    if (placeholder.resourceID == NEWRESOURCEID)
        return;

    Debug::log(TRACE, "Updating placeholder text: {}", newText);
    placeholder.currentText = newText;
    placeholder.asset       = nullptr;
    placeholder.resourceID  = NEWRESOURCEID;

    if (std::ranges::find(placeholder.registeredResourceIDs, placeholder.resourceID) != placeholder.registeredResourceIDs.end())
        return;

    Debug::log(TRACE, "Requesting new placeholder asset: {}", placeholder.resourceID);
    placeholder.registeredResourceIDs.push_back(placeholder.resourceID);

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = placeholder.resourceID;
    request.asset                = placeholder.currentText;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = colorState.font;
    request.props["font_size"]   = (int)size->value().y / 4;
    request.callback             = [REF = m_self] {
        if (const auto SELF = REF.lock(); SELF)
            g_pHyprlock->renderOutput(SELF->outputStringPort);
    };
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CPasswordInputField::updateWidth() {
    double targetSizeX = configSize.x;

    if (passwordLength == 0 && placeholder.asset)
        targetSizeX = placeholder.asset->texture.m_vSize.x + size->goal().y;

    targetSizeX = std::max(targetSizeX, configSize.x);

    if (size->goal().x != targetSizeX) {
        *size = Vector2D{targetSizeX, configSize.y};
        size->setCallbackOnEnd([this](auto) {
            redrawShadow = true;
            pos          = posFromHVAlign(viewport, size->value(), configPos, halign, valign);
        });
    }

    if (size->isBeingAnimated()) {
        redrawShadow = true;
        pos          = posFromHVAlign(viewport, size->value(), configPos, halign, valign);
    }
}

void CPasswordInputField::updateHiddenInputState() {
    if (!hiddenInputState.enabled || (size_t)hiddenInputState.lastPasswordLength == passwordLength)
        return;

    // randomize new thang
    hiddenInputState.lastPasswordLength = passwordLength;

    const auto BASEOK = colorConfig.hiddenBase.asOkLab();

    // convert to polar coordinates
    const auto OKICHCHROMA = std::sqrt(std::pow(BASEOK.a, 2) + std::pow(BASEOK.b, 2));

    // now randomly rotate the hue
    const double OKICHHUE = (rand() % 10000000 / 10000000.0) * M_PI * 4;

    // convert back to OkLab
    const Hyprgraphics::CColor newColor = Hyprgraphics::CColor::SOkLab{
        .l = BASEOK.l,
        .a = OKICHCHROMA * std::cos(OKICHHUE),
        .b = OKICHCHROMA * std::sin(OKICHHUE),
    };

    hiddenInputState.lastColor    = {newColor, 1.0};
    hiddenInputState.lastQuadrant = (hiddenInputState.lastQuadrant + rand() % 3 + 1) % 4;
}

void CPasswordInputField::updateColors() {
    const bool          BORDERLESS = outThick == 0;
    const bool          NUMLOCK    = (colorConfig.invertNum) ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock;

    CGradientValueData* targetGrad = nullptr;

    if (g_pHyprlock->m_bCapsLock && NUMLOCK && !colorConfig.both->m_bIsFallback)
        targetGrad = colorConfig.both;
    else if (g_pHyprlock->m_bCapsLock)
        targetGrad = colorConfig.caps;
    else if (NUMLOCK && !colorConfig.num->m_bIsFallback)
        targetGrad = colorConfig.num;

    if (checkWaiting)
        targetGrad = colorConfig.check;
    else if (displayFail && passwordLength == 0)
        targetGrad = colorConfig.fail;

    CGradientValueData* outerTarget = colorConfig.outer;
    CHyprColor          innerTarget = colorConfig.inner;
    CHyprColor          fontTarget  = (displayFail) ? colorConfig.fail->m_vColors.front() : colorConfig.font;

    if (targetGrad) {
        if (BORDERLESS && colorConfig.swapFont) {
            fontTarget = targetGrad->m_vColors.front();
        } else if (BORDERLESS && !colorConfig.swapFont) {
            innerTarget = targetGrad->m_vColors.front();
            // When changing the inner color, the font cannot be fail_color
            fontTarget = colorConfig.font;
        } else if (targetGrad) {
            outerTarget = targetGrad;
        }
    }

    if (!BORDERLESS && *outerTarget != colorState.outer->goal())
        *colorState.outer = *outerTarget;

    if (innerTarget != colorState.inner->goal())
        *colorState.inner = innerTarget;

    colorState.font = fontTarget;
}

void CPasswordInputField::togglePassword() {
    password.show = !password.show;

    if (password.show)
        updatePassword();

    g_pHyprlock->renderOutput(outputStringPort);
}

CBox CPasswordInputField::getBoundingBoxWl() const {
    return {
        Vector2D{pos.x, viewport.y - pos.y - size->value().y},
        size->value(),
    };
}

CBox CPasswordInputField::getEyeBox() {
    double eyeHeight = (int)(std::nearbyint(configSize.y * password.eye.size * 0.5f) * 2.f);
    auto   eyeSize   = Vector2D{eyeHeight, eyeHeight};

    CBox   inputFieldBox = getBoundingBoxWl();
    auto   padding       = (inputFieldBox.h - eyeSize.y) / 2.0;
    auto   eyePosition   = inputFieldBox.pos() + (password.eye.placement == "right" ? Vector2D{inputFieldBox.w - eyeSize.x - padding, padding} : Vector2D{padding, padding});

    return {eyePosition, eyeSize};
}

void CPasswordInputField::onHover(const Vector2D& pos) {
    CBox eyeBox = getEyeBox();

    if (eyeBox.containsPoint(pos) && password.allowToggle && !password.eye.hide) {
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    } else {
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
    }
}

bool CPasswordInputField::staticHover() const {
    return false;
}

void CPasswordInputField::onClick(uint32_t button, bool down, const Vector2D& pos) {
    if (!password.allowToggle || password.eye.hide || !down)
        return;

    CBox eyeBox = getEyeBox();

    if (eyeBox.containsPoint(pos)) {
        togglePassword();

        g_pHyprlock->renderOutput(outputStringPort);
    }
}
