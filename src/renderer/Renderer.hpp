#pragma once

#include <chrono>
#include <optional>
#include "Shader.hpp"
#include "../defines.hpp"
#include "../core/LockSurface.hpp"
#include "../helpers/AnimatedVariable.hpp"
#include "../helpers/Color.hpp"
#include "AsyncResourceGatherer.hpp"
#include "../config/ConfigDataValues.hpp"
#include "widgets/IWidget.hpp"
#include "Framebuffer.hpp"

typedef std::unordered_map<const CSessionLockSurface*, std::vector<SP<IWidget>>> widgetMap_t;

class CRenderer {
  public:
    CRenderer();

    struct SRenderFeedback {
        bool needsFrame = false;
    };

    struct SBlurParams {
        int                       size = 0, passes = 0;
        float                     noise = 0, contrast = 0, brightness = 0, vibrancy = 0, vibrancy_darkness = 0;
        std::optional<CHyprColor> colorize;
        float                     boostA = 1.0;
    };

    SRenderFeedback renderLock(const CSessionLockSurface& surf);

    void            renderRect(const CBox& box, const CHyprColor& col, int rounding = 0);
    void            renderBorder(const CBox& box, const CGradientValueData& gradient, int thickness, int rounding = 0, float alpha = 1.0);
    void            renderTexture(const CBox& box, const CTexture& tex, float a = 1.0, int rounding = 0, std::optional<eTransform> tr = {});
    void renderTextureMix(const CBox& box, const CTexture& tex, const CTexture& tex2, float a = 1.0, float mixFactor = 0.0, int rounding = 0, std::optional<eTransform> tr = {});
    void blurFB(const CFramebuffer& outfb, SBlurParams params);

    UP<CAsyncResourceGatherer>            asyncResourceGatherer;
    std::chrono::system_clock::time_point firstFullFrameTime;

    void                                  pushFb(GLint fb);
    void                                  popFb();

    void                                  removeWidgetsFor(const CSessionLockSurface* surf);

    void                                  startFadeIn();
    void                                  startFadeOut(bool unlock = false, bool immediate = true);

  private:
    widgetMap_t               widgets;

    std::vector<SP<IWidget>>* getOrCreateWidgetsFor(const CSessionLockSurface* surf);

    CShader                   rectShader;
    CShader                   texShader;
    CShader                   texMixShader;
    CShader                   blurShader1;
    CShader                   blurShader2;
    CShader                   blurPrepareShader;
    CShader                   blurFinishShader;
    CShader                   borderShader;

    Mat3x3                    projMatrix = Mat3x3::identity();
    Mat3x3                    projection;

    PHLANIMVAR<float>         opacity;

    std::vector<GLint>        boundFBs;
};

inline UP<CRenderer> g_pRenderer;
