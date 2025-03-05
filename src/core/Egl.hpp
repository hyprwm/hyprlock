#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "../defines.hpp"

class CEGL {
  public:
    CEGL(wl_display*);
    ~CEGL();

    EGLDisplay                               eglDisplay;
    EGLConfig                                eglConfig;
    EGLContext                               eglContext;

    PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT;

    void                                     makeCurrent(EGLSurface surf);
};

inline UP<CEGL> g_pEGL;
