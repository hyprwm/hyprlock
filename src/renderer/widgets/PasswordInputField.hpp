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
    CPasswordInputField(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props, const std::string& output);

    virtual bool draw(const SRenderData& data);
    void         onFadeOutTimer();

  private:
    void        updateDots();
    void        updateFade();
    void        updatePlaceholder();
    void        updateHiddenInputState();
    void        updateColors();

    bool        firstRender  = true;
    bool        redrawShadow = false;
    bool        checkWaiting = false;

    size_t      passwordLength = 0;
    size_t      failedAttempts = 0;

    Vector2D    size;
    Vector2D    pos;
    Vector2D    viewport;
    Vector2D    configPos;
    Vector2D    configSize;

    std::string halign, valign, configFailText, outputStringPort, configPlaceholderText;

    int         outThick, rounding;

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
        std::string              resourceID = "";
        SPreloadedAsset*         asset      = nullptr;

        std::string              currentText   = "";
        bool                     canGetNewText = true;
        bool                     isFailText    = false;

        std::string              lastAuthFeedback;

        std::vector<std::string> registeredResourceIDs;

    } placeholder;

    struct {
        CColor lastColor;
        int    lastQuadrant       = 0;
        int    lastPasswordLength = 0;
        bool   enabled            = false;
    } hiddenInputState;

    struct {
        CColor outer;
        CColor inner;
        CColor font;
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
        bool   swapFont     = false;
        bool   shouldStart;

        //
        std::chrono::system_clock::time_point lastFrame;
    } col;

    bool        fadeOnEmpty;
    uint64_t    fadeTimeoutMs;

    CShadowable shadow;
};
