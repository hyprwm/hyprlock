#pragma once

#include "IWidget.hpp"
#include "../../defines.hpp"
#include "../../helpers/Color.hpp"
#include "../../core/Timer.hpp"
#include "Shadowable.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "cursor-shape-v1.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <any>
#include <unordered_map>

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField() = default;
    virtual ~CPasswordInputField();

    void         registerSelf(const ASP<CPasswordInputField>& self);
    eWidgetType  getType() const;

    virtual void configure(const std::unordered_map<std::string, std::any>& prop, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);
    virtual void onAssetUpdate(ResourceID id, ASP<CTexture> newAsset);

    virtual void onHover(const Vector2D& pos);
    virtual void setCursorShape(wpCursorShapeDeviceV1Shape shape);
    virtual bool staticHover() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual CBox getBoundingBoxWl() const;

    void         reset();
    void         onFadeOutTimer();

    void         renderPasswordUpdate();

    void         togglePassword();

  private:
    AWP<CPasswordInputField>   m_self;

    void                       updatePassword();
    void                       updateEye();
    void                       updateDots();
    void                       updateFade();
    void                       updatePlaceholder();
    void                       updateWidth();
    void                       updateHiddenInputState();
    void                       updateInputState();
    void                       updateColors();

    void                       drawPasswordText(int eyeOffset, CHyprColor fontCol);
    bool                       drawPasswordDots(int eyeOffset, CHyprColor fontCol, const SRenderData& data);

    CBox                       getEyeBox();

    bool                       firstRender  = true;
    bool                       redrawShadow = false;
    bool                       checkWaiting = false;
    bool                       displayFail  = false;

    size_t                     passwordLength = 0;

    PHLANIMVAR<Vector2D>       size;
    Vector2D                   pos;
    Vector2D                   viewport;
    Vector2D                   configPos;
    Vector2D                   configSize;

    std::string                halign, valign, configFailText, outputStringPort, configPlaceholderText, fontFamily;
    uint64_t                   configFailTimeoutMs = 2000;

    int                        outThick, rounding;

    wpCursorShapeDeviceV1Shape cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;

    struct {
        bool allowToggle = false;
        bool show        = false;

        struct {
            float         size       = 0.25;
            bool          center     = false;
            std::string   content    = "";
            size_t        resourceID = 0;
            ASP<CTexture> asset      = nullptr;
        } text;

        struct {
            PHLANIMVAR<float> currentAmount;
            float             size       = 0.25;
            bool              center     = false;
            float             spacing    = 0;
            int               rounding   = 0;
            std::string       format     = "";
            size_t            resourceID = 9;
            ASP<CTexture>     asset      = nullptr;
        } dots;

        struct {
            int           margin    = 16;
            double        size      = 0.25;
            std::string   placement = "right";
            bool          hide      = false;

            size_t        openRescourceID   = 0;
            ASP<CTexture> openAsset         = nullptr;
            size_t        closedRescourceID = 9;
            ASP<CTexture> closedAsset       = nullptr;
        } eye;
    } password;

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
