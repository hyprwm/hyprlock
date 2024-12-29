#include "LockSurface.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "Egl.hpp"
#include "../renderer/Renderer.hpp"
#include "src/config/ConfigManager.hpp"

CSessionLockSurface::~CSessionLockSurface() {
    if (eglWindow)
        wl_egl_window_destroy(eglWindow);
}

CSessionLockSurface::CSessionLockSurface(COutput* output) : output(output) {
    surface = makeShared<CCWlSurface>(g_pHyprlock->getCompositor()->sendCreateSurface());

    if (!surface) {
        Debug::log(CRIT, "Couldn't create wl_surface");
        exit(1);
    }

    const auto PFRACTIONALSCALING = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:fractional_scaling");
    const auto ENABLE_FSV1        = **PFRACTIONALSCALING == 1 || /* auto enable */ (**PFRACTIONALSCALING == 2);
    const auto PFRACTIONALMGR     = g_pHyprlock->getFractionalMgr();
    const auto PVIEWPORTER        = g_pHyprlock->getViewporter();

    if (ENABLE_FSV1 && PFRACTIONALMGR && PVIEWPORTER) {
        fractional = makeShared<CCWpFractionalScaleV1>(PFRACTIONALMGR->sendGetFractionalScale(surface->resource()));

        fractional->setPreferredScale([this](CCWpFractionalScaleV1*, uint32_t scale) {
            const bool SAMESCALE = fractionalScale == scale / 120.0;
            fractionalScale      = scale / 120.0;

            Debug::log(LOG, "Got fractional scale: {:.1f}%", fractionalScale * 100.F);

            if (!SAMESCALE && readyForFrame)
                onScaleUpdate();
        });

        viewport = makeShared<CCWpViewport>(PVIEWPORTER->sendGetViewport(surface->resource()));
    }

    if (!PFRACTIONALMGR)
        Debug::log(LOG, "No fractional-scale support! Oops, won't be able to scale!");
    if (!PVIEWPORTER)
        Debug::log(LOG, "No viewporter support! Oops, won't be able to scale!");

    lockSurface = makeShared<CCExtSessionLockSurfaceV1>(g_pHyprlock->getSessionLock()->sendGetLockSurface(surface->resource(), output->output->resource()));

    if (!lockSurface) {
        Debug::log(CRIT, "Couldn't create ext_session_lock_surface_v1");
        exit(1);
    }

    lockSurface->setConfigure([this](CCExtSessionLockSurfaceV1* r, uint32_t serial, uint32_t width, uint32_t height) { configure({(double)width, (double)height}, serial); });
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
        viewport->sendSetDestination(logicalSize.x, logicalSize.y);
        surface->sendSetBufferScale(1);
    } else {
        size = size_ * output->scale;
        surface->sendSetBufferScale(output->scale);
    }

    if (!SAMESERIAL)
        lockSurface->sendAckConfigure(serial);

    Debug::log(LOG, "Configuring surface for logical {} and pixel {}", logicalSize, size);

    surface->sendDamageBuffer(0, 0, 0xFFFF, 0xFFFF);

    if (!eglWindow) {
        eglWindow = wl_egl_window_create((wl_surface*)surface->resource(), size.x, size.y);
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

void CSessionLockSurface::render() {
    Debug::log(TRACE, "render lock");

    if (frameCallback || !readyForFrame) {
        needsFrame = true;
        return;
    }

    const auto FEEDBACK = g_pRenderer->renderLock(*this);
    frameCallback       = makeShared<CCWlCallback>(surface->sendFrame());
    frameCallback->setDone([this](CCWlCallback* r, uint32_t data) {
        if (g_pHyprlock->m_bTerminate)
            return;

        onCallback();
    });

    eglSwapBuffers(g_pEGL->eglDisplay, eglSurface);

    needsFrame = FEEDBACK.needsFrame;
}

void CSessionLockSurface::onCallback() {
    frameCallback.reset();

    if (needsFrame && !g_pHyprlock->m_bTerminate && g_pEGL) {
        needsFrame = false;
        render();
    }
}