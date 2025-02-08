#pragma once

#include "../defines.hpp"
#include "wayland.hpp"
#include "ext-session-lock-v1.hpp"
#include "viewporter.hpp"
#include "fractional-scale-v1.hpp"
#include "../helpers/Math.hpp"
#include <wayland-egl.h>
#include <EGL/egl.h>

class COutput;
class CRenderer;

class CSessionLockSurface {
  public:
    CSessionLockSurface(const SP<COutput>& pOutput);
    ~CSessionLockSurface();

    void  configure(const Vector2D& size, uint32_t serial);

    bool  readyForFrame = false;

    float fractionalScale = 1.0;

    void  render();
    void  onCallback();
    void  onScaleUpdate();

  private:
    WP<COutput>                   m_outputRef;

    SP<CCWlSurface>               surface     = nullptr;
    SP<CCExtSessionLockSurfaceV1> lockSurface = nullptr;
    uint32_t                      serial      = 0;
    wl_egl_window*                eglWindow   = nullptr;
    Vector2D                      size;
    Vector2D                      logicalSize;
    float                         appliedScale;
    EGLSurface                    eglSurface = nullptr;
    SP<CCWpFractionalScaleV1>     fractional = nullptr;
    SP<CCWpViewport>              viewport   = nullptr;

    bool                          needsFrame = false;

    uint32_t                      m_lastFrameTime = 0;
    uint32_t                      m_frames        = 0;

    // wayland callbacks
    SP<CCWlCallback> frameCallback = nullptr;

    friend class CRenderer;
    friend class COutput;
};
