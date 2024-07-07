#include "LockSurface.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "Egl.hpp"
#include "../renderer/Renderer.hpp"

static void handleConfigure(void* data, ext_session_lock_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
    const auto PSURF = (CSessionLockSurface*)data;
    PSURF->configure({width, height}, serial);
}

static const ext_session_lock_surface_v1_listener lockListener = {
    .configure = handleConfigure,
};

static void handlePreferredScale(void* data, wp_fractional_scale_v1* wp_fractional_scale_v1, uint32_t scale) {
    const auto PSURF       = (CSessionLockSurface*)data;
    PSURF->fractionalScale = scale / 120.0;
    Debug::log(LOG, "Got fractional scale: {}", PSURF->fractionalScale);

    if (PSURF->readyForFrame)
        PSURF->onScaleUpdate();
}

static const wp_fractional_scale_v1_listener fsListener = {
    .preferred_scale = handlePreferredScale,
};

CSessionLockSurface::~CSessionLockSurface() {
    if (fractional) {
        wp_viewport_destroy(viewport);
        wp_fractional_scale_v1_destroy(fractional);
    }

    if (eglWindow)
        wl_egl_window_destroy(eglWindow);

    if (lockSurface)
        ext_session_lock_surface_v1_destroy(lockSurface);

    if (surface)
        wl_surface_destroy(surface);

    if (frameCallback)
        wl_callback_destroy(frameCallback);
}

CSessionLockSurface::CSessionLockSurface(COutput* output) : output(output) {
    surface = wl_compositor_create_surface(g_pHyprlock->getCompositor());

    if (!surface) {
        Debug::log(CRIT, "Couldn't create wl_surface");
        exit(1);
    }

    fractional = wp_fractional_scale_manager_v1_get_fractional_scale(g_pHyprlock->getFractionalMgr(), surface);
    if (fractional) {
        wp_fractional_scale_v1_add_listener(fractional, &fsListener, this);
        viewport = wp_viewporter_get_viewport(g_pHyprlock->getViewporter(), surface);
    } else
        Debug::log(LOG, "No fractional-scale support! Oops, won't be able to scale!");

    lockSurface = ext_session_lock_v1_get_lock_surface(g_pHyprlock->getSessionLock(), surface, output->output);

    if (!lockSurface) {
        Debug::log(CRIT, "Couldn't create ext_session_lock_surface_v1");
        exit(1);
    }

    ext_session_lock_surface_v1_add_listener(lockSurface, &lockListener, this);
}

void CSessionLockSurface::configure(const Vector2D& size_, uint32_t serial_) {
    Debug::log(LOG, "configure with serial {}", serial_);

    const bool sameSerial = serial == serial_;

    serial      = serial_;
    logicalSize = size_;

    if (fractional) {
        size = (size_ * fractionalScale).floor();
        wp_viewport_set_destination(viewport, logicalSize.x, logicalSize.y);
    } else {
        size = size_;
    }

    Debug::log(LOG, "Configuring surface for logical {} and pixel {}", logicalSize, size);

    if (!sameSerial)
        ext_session_lock_surface_v1_ack_configure(lockSurface, serial);

    wl_surface_set_buffer_scale(surface, 1);
    wl_surface_damage_buffer(surface, 0, 0, 0xFFFF, 0xFFFF);

    if (!eglWindow) {
        eglWindow = wl_egl_window_create(surface, size.x, size.y);
        if (!eglWindow) {
            Debug::log(CRIT, "Couldn't create eglWindow");
            exit(1);
        }
    } else
        wl_egl_window_resize(eglWindow, size.x, size.y, 0, 0);

    if (!eglSurface) {
        eglSurface = g_pEGL->eglCreatePlatformWindowSurfaceEXT(g_pEGL->eglDisplay, g_pEGL->eglConfig, eglWindow, nullptr);
        if (!eglSurface) {
            Debug::log(CRIT, "Couldn't create eglSurface: {}", (int)eglGetError());
            // Clean up resources to prevent leaks
            wl_egl_window_destroy(eglWindow);
            eglWindow = nullptr;
            exit(1); // Consider graceful exit or fallback
        }
    }

    readyForFrame = true;

    render();
}

void CSessionLockSurface::onScaleUpdate() {
    configure(logicalSize, serial);
}

static void handleDone(void* data, wl_callback* wl_callback, uint32_t callback_data) {
    const auto PSURF = (CSessionLockSurface*)data;

    if (g_pHyprlock->m_bTerminate)
        return;

    PSURF->onCallback();
}

static const wl_callback_listener callbackListener = {
    .done = handleDone,
};

void CSessionLockSurface::render() {
    Debug::log(TRACE, "render lock");

    if (frameCallback || !readyForFrame)
        return;

    const auto FEEDBACK = g_pRenderer->renderLock(*this);
    frameCallback       = wl_surface_frame(surface);
    wl_callback_add_listener(frameCallback, &callbackListener, this);

    eglSwapBuffers(g_pEGL->eglDisplay, eglSurface);

    needsFrame = FEEDBACK.needsFrame;
}

void CSessionLockSurface::onCallback() {
    wl_callback_destroy(frameCallback);
    frameCallback = nullptr;

    if (needsFrame && !g_pHyprlock->m_bTerminate && g_pEGL) {
        needsFrame = false;
        render();
    }
}