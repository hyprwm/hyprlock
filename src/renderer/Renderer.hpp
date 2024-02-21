#pragma once

#include <memory>
#include <chrono>

#include "../core/LockSurface.hpp"
#include "Shader.hpp"
#include "../helpers/Box.hpp"
#include "../helpers/Color.hpp"
#include "AsyncResourceGatherer.hpp"
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
        int   size = 0, passes = 0;
        float noise = 0, contrast = 0, brightness = 0, vibrancy = 0, vibrancy_darkness = 0;
    };

    SRenderFeedback                         renderLock(const CSessionLockSurface& surface);

    void                                    renderRect(const CBox& box, const CColor& col, int rounding = 0);
    void                                    renderTexture(const CBox& box, const CTexture& tex, float a = 1.0, int rounding = 0, bool noTransform = false);
    void                                    blurTexture(const CFramebuffer& outfb, const CTexture& tex, SBlurParams params);

    std::unique_ptr<CAsyncResourceGatherer> asyncResourceGatherer;
    std::chrono::system_clock::time_point   gatheredAt;

  private:
    widgetMap_t                            widgets;

    std::vector<std::unique_ptr<IWidget>>* getOrCreateWidgetsFor(const CSessionLockSurface* surf);

    CShader                                rectShader;
    CShader                                texShader;
    CShader                                blurShader1;
    CShader                                blurShader2;
    CShader                                blurPrepareShader;
    CShader                                blurFinishShader;

    std::array<float, 9>                   projMatrix;
    std::array<float, 9>                   projection;
};

inline std::unique_ptr<CRenderer> g_pRenderer;