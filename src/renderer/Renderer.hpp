#pragma once

#include <memory>
#include <chrono>
#include <optional>
#include "Shader.hpp"
#include "../core/LockSurface.hpp"
#include "../helpers/Color.hpp"
#include "AsyncResourceGatherer.hpp"
#include "../config/ConfigDataValues.hpp"
#include "widgets/IWidget.hpp"
#include "Framebuffer.hpp"

typedef std::unordered_map<const CSessionLockSurface*, std::vector<std::unique_ptr<IWidget>>> widgetMap_t;

class CRenderer {
  public:
    CRenderer();

    struct SRenderFeedback {
        bool needsFrame = false;
    };

    struct SBlurParams {
        int                   size = 0, passes = 0;
        float                 noise = 0, contrast = 0, brightness = 0, vibrancy = 0, vibrancy_darkness = 0;
        std::optional<CColor> colorize;
        float                 boostA = 1.0;
    };

    SRenderFeedback                         renderLock(const CSessionLockSurface& surface);

    void                                    renderRect(const CBox& box, const CColor& col, int rounding = 0);
    void                                    renderBorder(const CBox& box, const CGradientValueData& gradient, int thickness, int rounding = 0, float alpha = 1.0);
    void                                    renderTexture(const CBox& box, const CTexture& tex, float a = 1.0, int rounding = 0, std::optional<eTransform> tr = {});
    void                                    blurFB(const CFramebuffer& outfb, SBlurParams params);

    std::unique_ptr<CAsyncResourceGatherer> asyncResourceGatherer;
    std::chrono::system_clock::time_point   firstFullFrameTime;

    void                                    pushFb(GLint fb);
    void                                    popFb();

    void                                    removeWidgetsFor(const CSessionLockSurface* surf);

  private:
    widgetMap_t                            widgets;

    std::vector<std::unique_ptr<IWidget>>* getOrCreateWidgetsFor(const CSessionLockSurface* surf);

    CShader                                rectShader;
    CShader                                texShader;
    CShader                                blurShader1;
    CShader                                blurShader2;
    CShader                                blurPrepareShader;
    CShader                                blurFinishShader;
    CShader                                borderShader;

    Mat3x3                                 projMatrix = Mat3x3::identity();
    Mat3x3                                 projection;

    std::vector<GLint>                     boundFBs;
};

inline std::unique_ptr<CRenderer> g_pRenderer;
