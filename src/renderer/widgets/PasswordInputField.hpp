#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../core/Timer.hpp"
#include "Shadowable.hpp"
#include "src/config/ConfigDataValues.hpp"
#include <chrono>
#include <vector>
#include <any>
#include <unordered_map>

struct SPreloadedAsset;

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props, const std::string& output);

    virtual bool draw(const SRenderData& data);
    void         onFadeOutTimer();

  private:
    void        updateDots();
    void        updateFade();
    void        updatePlaceholder();
    void        updateWidth();
    void        updateHiddenInputState();
    void        updateInputState();
    void        updateColors();

    bool        firstRender  = true;
    bool        redrawShadow = false;
    bool        checkWaiting = false;
    bool        displayFail  = false;

    size_t      passwordLength = 0;

    Vector2D    size;
    Vector2D    pos;
    Vector2D    viewport;
    Vector2D    configPos;
    Vector2D    configSize;

    std::string halign, valign, configFailText, outputStringPort, configPlaceholderText, fontFamily;
    uint64_t    configFailTimeoutMs = 2000;

    int         outThick, rounding;

    struct {
        std::chrono::system_clock::time_point start;
        bool                                  animated = false;
        double                                source   = 0;
    } dynamicWidth;

    struct {
        float                                 currentAmount = 0;
        int                                   fadeMs        = 0;
        std::chrono::system_clock::time_point lastFrame;
        bool                                  center     = false;
        float                                 size       = 0;
        float                                 spacing    = 0;
        int                                   rounding   = 0;
        std::string                           textFormat = "";
        SPreloadedAsset*                      textAsset  = nullptr;
        std::string                           textResourceID;
    } dots;

    struct {
        std::chrono::system_clock::time_point start;
        float                                 a            = 0;
        bool                                  appearing    = true;
        bool                                  animated     = false;
        std::shared_ptr<CTimer>               fadeOutTimer = nullptr;
        bool                                  allowFadeOut = false;
    } fade;

    struct {
        std::string              resourceID = "";
        SPreloadedAsset*         asset      = nullptr;

        std::string              currentText    = "";
        size_t                   failedAttempts = 0;
        bool                     canGetNewText  = true;

        std::string              lastAuthFeedback;

        std::vector<std::string> registeredResourceIDs;

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

        int                 transitionMs = 0;
        bool                invertNum    = false;
        bool                swapFont     = false;
    } colorConfig;

    struct {
        CGradientValueData  outer;
        CHyprColor          inner;
        CHyprColor          font;

        CGradientValueData* outerSource = nullptr;
        CHyprColor          innerSource;

        CGradientValueData* currentTarget = nullptr;

        bool                animated = false;

        //
        std::chrono::system_clock::time_point lastFrame;
    } colorState;

    bool        fadeOnEmpty;
    uint64_t    fadeTimeoutMs;

    CShadowable shadow;
};
