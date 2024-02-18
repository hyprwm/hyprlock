#include "Egl.hpp"
#include "../helpers/Log.hpp"

PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;

const EGLint                    config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE,
};

const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION,
    2,
    EGL_NONE,
};

CEGL::CEGL(wl_display* display) {
    const char* _EXTS = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!_EXTS) {
        if (eglGetError() == EGL_BAD_DISPLAY)
            throw std::runtime_error("EGL_EXT_client_extensions not supported");
        else
            throw std::runtime_error("Failed to query EGL client extensions");
    }

    std::string EXTS = _EXTS;

    if (!EXTS.contains("EGL_EXT_platform_base"))
        throw std::runtime_error("EGL_EXT_platform_base not supported");

    if (!EXTS.contains("EGL_EXT_platform_wayland"))
        throw std::runtime_error("EGL_EXT_platform_wayland not supported");

    eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (eglGetPlatformDisplayEXT == NULL)
        throw std::runtime_error("Failed to get eglGetPlatformDisplayEXT");

    eglCreatePlatformWindowSurfaceEXT = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    if (eglCreatePlatformWindowSurfaceEXT == NULL)
        throw std::runtime_error("Failed to get eglCreatePlatformWindowSurfaceEXT");

    eglDisplay     = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, display, NULL);
    EGLint matched = 0;
    if (eglDisplay == EGL_NO_DISPLAY) {
        Debug::log(CRIT, "Failed to create EGL display");
        goto error;
    }

    if (eglInitialize(eglDisplay, NULL, NULL) == EGL_FALSE) {
        Debug::log(CRIT, "Failed to initialize EGL");
        goto error;
    }

    if (!eglChooseConfig(eglDisplay, config_attribs, &eglConfig, 1, &matched)) {
        Debug::log(CRIT, "eglChooseConfig failed");
        goto error;
    }
    if (matched == 0) {
        Debug::log(CRIT, "Failed to match an EGL config");
        goto error;
    }

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, context_attribs);
    if (eglContext == EGL_NO_CONTEXT) {
        Debug::log(CRIT, "Failed to create EGL context");
        goto error;
    }

    return;

error:
    eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (eglDisplay)
        eglTerminate(eglDisplay);

    eglReleaseThread();
}

void CEGL::makeCurrent(EGLSurface surf) {
    eglMakeCurrent(eglDisplay, surf, surf, eglContext);
}