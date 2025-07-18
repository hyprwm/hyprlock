#include "CursorShape.hpp"
#include "hyprlock.hpp"
#include "Seat.hpp"

CCursorShape::CCursorShape(SP<CCWpCursorShapeManagerV1> mgr) : mgr(mgr) {
    if (!g_pSeatManager->m_pPointer)
        return;

    dev = makeShared<CCWpCursorShapeDeviceV1>(mgr->sendGetPointer(g_pSeatManager->m_pPointer->resource()));
}

void CCursorShape::setShape(const wpCursorShapeDeviceV1Shape shape) {
    if (!g_pSeatManager->m_pPointer)
        return;

    if (!dev)
        dev = makeShared<CCWpCursorShapeDeviceV1>(mgr->sendGetPointer(g_pSeatManager->m_pPointer->resource()));

    dev->sendSetShape(lastCursorSerial, shape);
}

void CCursorShape::hideCursor(wl_proxy* surf, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    m_hidden                      = true;
    m_cursorStateBackup.surf      = surf;
    m_cursorStateBackup.surface_x = surface_x;
    m_cursorStateBackup.surface_y = surface_y;

    g_pSeatManager->m_pPointer->sendSetCursor(lastCursorSerial, nullptr, 0, 0);
}

void CCursorShape::restoreCursor() {
    if (!g_pSeatManager->m_pPointer)
        return;

    if (m_hidden) {
        for (const auto& POUTPUT : g_pHyprlock->m_vOutputs) {
            if (!POUTPUT->m_sessionLockSurface)
                continue;

            const auto& PWLSURFACE = POUTPUT->m_sessionLockSurface->getWlSurface();
            if (PWLSURFACE->resource() == m_cursorStateBackup.surf)
                g_pSeatManager->m_pPointer->sendSetCursor(lastCursorSerial, PWLSURFACE.get(), m_cursorStateBackup.surface_x, m_cursorStateBackup.surface_y);
        }

        m_hidden = false;
    }

    setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}
