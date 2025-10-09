#pragma once

#include "IWidget.hpp"
#include "../../defines.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "Shadowable.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <vector>
#include <any>
#include <unordered_map>

struct SPreloadedAsset;

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField() = default;
    virtual ~CPasswordInputField();

    void         registerSelf(const ASP<CPasswordInputField>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);
    virtual void onAssetUpdate(ResourceID id, ASP<CTexture> newAsset);

    virtual void onHover(const Vector2D& pos);
    virtual CBox getBoundingBoxWl() const;

    void         reset();
    void         onFadeOutTimer();

  private:
    AWP<CPasswordInputField> m_self;

    void                     updateDots();
    void                     updateFade();
    void                     updatePlaceholder();
    void                     updateWidth();
    void                     updateHiddenInputState();
    void                     updateInputState();
    void                     updateColors();

    bool                     firstRender  = true;
    bool                     redrawShadow = false;
    bool                     checkWaiting = false;
    bool                     displayFail  = false;

    size_t                   passwordLength = 0;

    PHLANIMVAR<Vector2D>     size;
    Vector2D                 pos;
    Vector2D                 viewport;
    Vector2D                 configPos;
    Vector2D                 configSize;

    std::string              halign, valign, configFailText, outputStringPort, configPlaceholderText, fontFamily;
    uint64_t                 configFailTimeoutMs = 2000;

    int                      outThick, rounding;

    struct {
        PHLANIMVAR<float> currentAmount;
        bool              center         = false;
        float             size           = 0;
        float             spacing        = 0;
        int               rounding       = 0;
        size_t            textResourceID = 0;
        std::string       textFormat     = "";
        ASP<CTexture>     textAsset      = nullptr;
    } dots;

    struct {
        PHLANIMVAR<float> a;
        bool              appearing    = true;
        ASP<CTimer>       fadeOutTimer = nullptr;
        bool              allowFadeOut = false;
    } fade;

    struct {
        size_t        resourceID = 0;
        ASP<CTexture> asset      = nullptr;

        std::string   currentText    = "";
        size_t        failedAttempts = 0;
    } placeholder;

    struct {
        CHyprColor lastColor;
        int        lastQuadrant       = 0;
        int        lastPasswordLength = 0;
        bool       enabled            = false;
    } hiddenInputState;

    struct {
        CGradientValueData* outer = nullptr;
        CHyprColor          inner;
        CHyprColor          font;
        CGradientValueData* fail  = nullptr;
        CGradientValueData* check = nullptr;
        CGradientValueData* caps  = nullptr;
        CGradientValueData* num   = nullptr;
        CGradientValueData* both  = nullptr;

        CHyprColor          hiddenBase;

        int                 transitionMs = 0;
        bool                invertNum    = false;
        bool                swapFont     = false;
    } colorConfig;

    struct {
        PHLANIMVAR<CGradientValueData> outer;
        PHLANIMVAR<CHyprColor>         inner;
        // Font color is only chaned, when `swap_font_color` is set to true and no border is present.
        // It is not animated, because that does not look good and we would need to rerender the text for each frame.
        CHyprColor font;
    } colorState;

    bool        fadeOnEmpty;
    uint64_t    fadeTimeoutMs;

    CShadowable shadow;
};
