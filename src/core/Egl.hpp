#pragma once

#include <wayland-client.h>
#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>

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

inline std::unique_ptr<CEGL> g_pEGL;