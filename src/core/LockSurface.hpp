#pragma once

#include <wayland-client.h>
#include "ext-session-lock-v1-protocol.h"
#include "viewporter-protocol.h"
#include "fractional-scale-v1-protocol.h"
#include <wayland-egl.h>
#include "../helpers/Vector2D.hpp"
#include <EGL/egl.h>
#include <optional>

class COutput;
class CRenderer;

class CSessionLockSurface {
  public:
    CSessionLockSurface(COutput* output);
    ~CSessionLockSurface();

    void  configure(const Vector2D& size, std::optional<uint32_t> serial);

    bool  readyForFrame = false;

    float fractionalScale = 1.0;

    void  render();
    void  onCallback();

  private:
    COutput*                     output      = nullptr;
    wl_surface*                  surface     = nullptr;
    ext_session_lock_surface_v1* lockSurface = nullptr;
    uint32_t                     serial      = 0;
    wl_egl_window*               eglWindow   = nullptr;
    Vector2D                     size;
    Vector2D                     logicalSize;
    EGLSurface                   eglSurface = nullptr;
    wp_fractional_scale_v1*      fractional = nullptr;
    wp_viewport*                 viewport   = nullptr;

    bool                         needsFrame = false;

    // wayland callbacks
    wl_callback* frameCallback = nullptr;

    friend class CRenderer;
};