#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include "../../core/Timer.hpp"
#include "Shadowable.hpp"
#include <chrono>
#include <vector>
#include <any>
#include <unordered_map>

struct SPreloadedAsset;

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props);

    virtual bool draw(const SRenderData& data);
    void         onFadeOutTimer();

  private:
    void        updateDots();
    void        updateFade();
    void        updateFailTex();
    void        updateHiddenInputState();
    void        updateOuter();

    bool        firstRender  = true;
    bool        redrawShadow = false;
    bool        checkWaiting = false;

    size_t      passwordLength = 0;

    Vector2D    size;
    Vector2D    pos;
    Vector2D    viewport;
    Vector2D    configPos;
    Vector2D    configSize;

    std::string halign, valign, configFailText;

    int         outThick, rounding;

    CColor      inner, font;

    struct {
        float                                 currentAmount  = 0;
        float                                 speedPerSecond = 5; // actually per... something. I am unsure xD
        std::chrono::system_clock::time_point lastFrame;
        bool                                  center   = false;
        float                                 size     = 0;
        float                                 spacing  = 0;
        int                                   rounding = 0;
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
        std::string      resourceID = "";
        SPreloadedAsset* asset      = nullptr;

        std::string      failID        = "";
        SPreloadedAsset* failAsset     = nullptr;
        bool             canGetNewFail = true;
        std::string      failText      = "";
    } placeholder;

    struct {
        CColor lastColor;
        int    lastQuadrant       = 0;
        int    lastPasswordLength = 0;
        bool   enabled            = false;
    } hiddenInputState;

    struct {
        CColor main;
        CColor fail;
        CColor check;
        CColor caps;
        CColor num;
        CColor both;
        int    transitionMs = 0;
        bool   invertNum    = false;
        bool   animated     = false;
        bool   stateNum     = false;
        bool   stateCaps    = false;
    } outerColor;

    bool        fadeOnEmpty;
    uint64_t    fadeTimeoutMs;

    CShadowable shadow;
};
