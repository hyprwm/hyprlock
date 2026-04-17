#include "LockSurface.hpp"
#include "hyprlock.hpp"
#include "Egl.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/AnimationManager.hpp"
#include "../helpers/Log.hpp"
#include "../renderer/Renderer.hpp"

CSessionLockSurface::~CSessionLockSurface() {
    if (frameCallback)
        frameCallback.reset();

    if (eglSurface)
        eglDestroySurface(g_pEGL->eglDisplay, eglSurface);

    if (eglWindow)
        wl_egl_window_destroy(eglWindow);
}

CSessionLockSurface::CSessionLockSurface(const SP<COutput>& pOutput) : m_outputRef(pOutput), m_outputID(pOutput->m_ID) {
    surface = makeShared<CCWlSurface>(g_pHyprlock->getCompositor()->sendCreateSurface());
    RASSERT(surface, "Couldn't create wl_surface");

    static const auto FRACTIONALSCALING = g_pConfigManager->getValue<Hyprlang::INT>("general:fractional_scaling");
    const auto        ENABLE_FSV1       = *FRACTIONALSCALING == 1 || /* auto enable */ (*FRACTIONALSCALING == 2);
    const auto        PFRACTIONALMGR    = g_pHyprlock->getFractionalMgr();
    const auto        PVIEWPORTER       = g_pHyprlock->getViewporter();

    if (ENABLE_FSV1 && PFRACTIONALMGR && PVIEWPORTER) {
        fractional = makeShared<CCWpFractionalScaleV1>(PFRACTIONALMGR->sendGetFractionalScale(surface->resource()));

        fractional->setPreferredScale([this](CCWpFractionalScaleV1*, uint32_t scale) {
            const bool SAMESCALE = fractionalScale == scale / 120.0;
            fractionalScale      = scale / 120.0;

            Log::logger->log(Log::INFO, "Got fractional scale: {:.1f}%", fractionalScale * 100.F);

            if (!SAMESCALE && readyForFrame)
                onScaleUpdate();
        });

        viewport = makeShared<CCWpViewport>(PVIEWPORTER->sendGetViewport(surface->resource()));
    }

    if (!PFRACTIONALMGR)
        Log::logger->log(Log::INFO, "No fractional-scale support! Oops, won't be able to scale!");
    if (!PVIEWPORTER)
        Log::logger->log(Log::INFO, "No viewporter support! Oops, won't be able to scale!");

    lockSurface = makeShared<CCExtSessionLockSurfaceV1>(g_pHyprlock->getSessionLock()->sendGetLockSurface(surface->resource(), pOutput->m_wlOutput->resource()));
    RASSERT(lockSurface, "Couldn't create ext_session_lock_surface_v1");

    lockSurface->setConfigure([this](CCExtSessionLockSurfaceV1* r, uint32_t serial, uint32_t width, uint32_t height) { configure({(double)width, (double)height}, serial); });
}

void CSessionLockSurface::configure(const Vector2D& size_, uint32_t serial_) {
    Log::logger->log(Log::INFO, "configure with serial {}", serial_);

    const bool SAMESERIAL = serial == serial_;
    const bool SAMESIZE   = logicalSize == size_;
    const bool SAMESCALE  = appliedScale == fractionalScale;

    const auto POUTPUT = m_outputRef.lock();

    serial       = serial_;
    logicalSize  = size_;
    appliedScale = fractionalScale;

    if (fractional) {
        size = (size_ * fractionalScale).floor();
        viewport->sendSetDestination(logicalSize.x, logicalSize.y);
        surface->sendSetBufferScale(1);
    } else {
        size = size_ * POUTPUT->scale;
        surface->sendSetBufferScale(POUTPUT->scale);
    }

    if (!SAMESERIAL)
        lockSurface->sendAckConfigure(serial);

    Log::logger->log(Log::INFO, "Configuring surface for logical {} and pixel {}", logicalSize, size);

    surface->sendDamageBuffer(0, 0, 0xFFFF, 0xFFFF);

    if (eglWindow && eglSurface) {
        Log::logger->log(Log::INFO, "Resizing existing eglWindow");
        wl_egl_window_resize(eglWindow, size.x, size.y, 0, 0);
    } else {
        if (eglWindow)
            wl_egl_window_destroy(eglWindow);

        eglWindow = wl_egl_window_create((wl_surface*)surface->resource(), size.x, size.y);
        if (!eglWindow) {
            // Only fails when unable to allocate the wl_egl_window structure or size x or y is <= 0.
            Log::logger->log(Log::CRIT, "Failed to create wayland egl window");
            readyForFrame = false;
            return;
        }

        if (eglSurface)
            eglDestroySurface(g_pEGL->eglDisplay, eglSurface);

        eglSurface = g_pEGL->eglCreatePlatformWindowSurfaceEXT(g_pEGL->eglDisplay, g_pEGL->eglConfig, eglWindow, nullptr);
        if (eglSurface == EGL_NO_SURFACE) {
            EGLint eglError = eglGetError();
            if (eglError == EGL_BAD_ALLOC)
                Log::logger->log(Log::CRIT, "Failed to allocate egl window surface (EGL_BAD_ALLOC, GPU memory pressure?)");
            else
                Log::logger->log(Log::CRIT, "Failed to create egl window surface");

            readyForFrame = false;
            return;
        }
    }

    if (readyForFrame && !(SAMESIZE && SAMESCALE)) {
        Log::logger->log(Log::INFO, "output {} changed, reloading widgets!", POUTPUT->stringPort);
        g_pRenderer->reconfigureWidgetsFor(POUTPUT->m_ID);
    }

    readyForFrame = true;

    render();
}

void CSessionLockSurface::onScaleUpdate() {
    configure(logicalSize, serial);
}

void CSessionLockSurface::render() {
    if (frameCallback || !readyForFrame) {
        needsFrame = true;
        return;
    }

    g_pAnimationManager->tick();
    const auto FEEDBACK = g_pRenderer->renderLock(*this);
    frameCallback       = makeShared<CCWlCallback>(surface->sendFrame());
    frameCallback->setDone([this](CCWlCallback* r, uint32_t frameTime) {
        if (g_pHyprlock->m_bTerminate)
            return;

        if (Log::logger->verbose()) {
            const auto POUTPUT = m_outputRef.lock();
            Log::logger->log(Log::TRACE, "[{}] frame {}, Current fps: {:.2f}", POUTPUT->stringPort, m_frames, 1000.f / (frameTime - m_lastFrameTime));
        }

        m_lastFrameTime = frameTime;

        m_frames++;

        onCallback();
    });

    if (eglSwapBuffers(g_pEGL->eglDisplay, eglSurface) != EGL_TRUE) {
        frameCallback.reset();
        needsFrame = true;
        return;
    }

    needsFrame = FEEDBACK.needsFrame || g_pAnimationManager->shouldTickForNext();
}

void CSessionLockSurface::onCallback() {
    frameCallback.reset();

    if (needsFrame && !g_pHyprlock->m_bTerminate && g_pEGL) {
        needsFrame = false;
        render();
    }
}

SP<CCWlSurface> CSessionLockSurface::getWlSurface() {
    return surface;
}
