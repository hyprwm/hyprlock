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
    if (eglGetPlatformDisplayEXT == nullptr)
        throw std::runtime_error("Failed to get eglGetPlatformDisplayEXT");

    eglCreatePlatformWindowSurfaceEXT = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    if (eglCreatePlatformWindowSurfaceEXT == nullptr)
        throw std::runtime_error("Failed to get eglCreatePlatformWindowSurfaceEXT");

    const char* vendorString = nullptr;
    eglDisplay               = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, display, nullptr);
    EGLint matched           = 0;
    if (eglDisplay == EGL_NO_DISPLAY) {
        Log::logger->log(Log::CRIT, "Failed to create EGL display");
        goto error;
    }

    if (eglInitialize(eglDisplay, nullptr, nullptr) == EGL_FALSE) {
        Log::logger->log(Log::CRIT, "Failed to initialize EGL");
        goto error;
    }

    if (!eglChooseConfig(eglDisplay, config_attribs, &eglConfig, 1, &matched)) {
        Log::logger->log(Log::CRIT, "eglChooseConfig failed");
        goto error;
    }
    if (matched == 0) {
        Log::logger->log(Log::CRIT, "Failed to match an EGL config");
        goto error;
    }

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, context_attribs);
    if (eglContext == EGL_NO_CONTEXT) {
        Log::logger->log(Log::CRIT, "Failed to create EGL context");
        goto error;
    }

    vendorString = eglQueryString(eglDisplay, EGL_VENDOR);
    m_isNvidia   = (vendorString) ? std::string{vendorString}.contains("NVIDIA") : false;

    return;

error:
    eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

CEGL::~CEGL() {
    if (eglContext != EGL_NO_CONTEXT)
        eglDestroyContext(eglDisplay, eglContext);

    if (eglDisplay)
        eglTerminate(eglDisplay);

    eglReleaseThread();
}

static const char* eglErrorToString(EGLint error) {
    switch (error) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_DEVICE_EXT: return "EGL_BAD_DEVICE_EXT";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
    }
    return "Unknown";
}

EGLSurface CEGL::createPlatformWindowSurfaceEXT(wl_egl_window* eglWindow) {
    EGLSurface eglSurface = eglCreatePlatformWindowSurfaceEXT(g_pEGL->eglDisplay, g_pEGL->eglConfig, eglWindow, nullptr);
    if (eglSurface == EGL_NO_SURFACE)
        Log::logger->log(Log::ERR, "Failed to allocate egl window surface, error: {}", eglErrorToString(eglGetError()));

    return eglSurface;
}

void CEGL::makeCurrent(EGLSurface surf) {
    if (eglMakeCurrent(eglDisplay, surf, surf, eglContext) == EGL_FALSE)
        Log::logger->log(Log::ERR, "Failed to eglMakeCurrent, error:  {}", eglErrorToString(eglGetError()));
}

bool CEGL::swapBuffers(EGLSurface surf) {
    if (eglSwapBuffers(eglDisplay, surf) == EGL_FALSE) {
        Log::logger->log(Log::ERR, "Failed to eglSwapBuffers, error:  {}", eglErrorToString(eglGetError()));
        return false;
    }

    return true;
}
