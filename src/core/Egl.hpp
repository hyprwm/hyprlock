#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>

#include "../defines.hpp"

class CEGL {
  public:
    CEGL(wl_display*);
    ~CEGL();

    EGLDisplay eglDisplay;
    EGLConfig  eglConfig;
    EGLContext eglContext;

    EGLSurface createPlatformWindowSurfaceEXT(wl_egl_window* eglWindow);
    void       makeCurrent(EGLSurface surf);
    bool       swapBuffers(EGLSurface surf);

    bool       m_isNvidia = false;

  private:
    PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT;
};

inline UP<CEGL> g_pEGL;
