#include "LockSurface.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "Egl.hpp"
#include "../renderer/Renderer.hpp"
#include "src/config/ConfigManager.hpp"

static void handleConfigure(void* data, ext_session_lock_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
    const auto PSURF = (CSessionLockSurface*)data;
    PSURF->configure({(double)width, (double)height}, serial);
}

static const ext_session_lock_surface_v1_listener lockListener = {
    .configure = handleConfigure,
};

static void handlePreferredScale(void* data, wp_fractional_scale_v1* wp_fractional_scale_v1, uint32_t scale) {
    const auto PSURF       = (CSessionLockSurface*)data;
    const bool SAMESCALE   = PSURF->fractionalScale == scale / 120.0;
    PSURF->fractionalScale = scale / 120.0;

    Debug::log(LOG, "Got fractional scale: {}", PSURF->fractionalScale);

    if (!SAMESCALE && PSURF->readyForFrame)
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

    const auto PFRACTIONALSCALING = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:fractional_scaling");
    const auto ENABLE_FSV1        = **PFRACTIONALSCALING == 1 || /* auto enable */ (**PFRACTIONALSCALING == 2);
    const auto PFRACTIONALMGR     = g_pHyprlock->getFractionalMgr();
    const auto PVIEWPORTER        = g_pHyprlock->getViewporter();

    if (ENABLE_FSV1 && PFRACTIONALMGR && PVIEWPORTER) {
        fractional = wp_fractional_scale_manager_v1_get_fractional_scale(PFRACTIONALMGR, surface);
        if (fractional) {
            wp_fractional_scale_v1_add_listener(fractional, &fsListener, this);
            viewport = wp_viewporter_get_viewport(PVIEWPORTER, surface);
        }
    }

    if (!PFRACTIONALMGR)
        Debug::log(LOG, "No fractional-scale support! Oops, won't be able to scale!");
    if (!PVIEWPORTER)
        Debug::log(LOG, "No viewporter support! Oops, won't be able to scale!");

    lockSurface = ext_session_lock_v1_get_lock_surface(g_pHyprlock->getSessionLock(), surface, output->output);

    if (!lockSurface) {
        Debug::log(CRIT, "Couldn't create ext_session_lock_surface_v1");
        exit(1);
    }

    ext_session_lock_surface_v1_add_listener(lockSurface, &lockListener, this);
}

void CSessionLockSurface::configure(const Vector2D& size_, uint32_t serial_) {
    Debug::log(LOG, "configure with serial {}", serial_);

    const bool SAMESERIAL = serial == serial_;
    const bool SAMESIZE   = logicalSize == size_;
    const bool SAMESCALE  = appliedScale == fractionalScale;

    serial       = serial_;
    logicalSize  = size_;
    appliedScale = fractionalScale;

    if (fractional) {
        size = (size_ * fractionalScale).floor();
        wp_viewport_set_destination(viewport, logicalSize.x, logicalSize.y);
        wl_surface_set_buffer_scale(surface, 1);
    } else {
        size = size_ * output->scale;
        wl_surface_set_buffer_scale(surface, output->scale);
    }

    if (!SAMESERIAL)
        ext_session_lock_surface_v1_ack_configure(lockSurface, serial);

    Debug::log(LOG, "Configuring surface for logical {} and pixel {}", logicalSize, size);

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

    if (readyForFrame && !(SAMESIZE && SAMESCALE)) {
        g_pRenderer->removeWidgetsFor(this);
        Debug::log(LOG, "Reloading widgets");
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

    if (frameCallback || !readyForFrame) {
        needsFrame = true;
        return;
    }

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