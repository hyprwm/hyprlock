#include "CursorShape.hpp"
#include "Seat.hpp"

CCursorShape::CCursorShape(SP<CCWpCursorShapeManagerV1> mgr) : mgr(mgr) {
    if (!g_pSeatManager->m_pPointer)
        return;

    dev = makeShared<CCWpCursorShapeDeviceV1>(mgr->sendGetPointer(g_pSeatManager->m_pPointer->resource()));
}

void CCursorShape::setShape(const wpCursorShapeDeviceV1Shape shape) {
    if (!dev)
        return;

    dev->sendSetShape(lastCursorSerial, shape);
}

void CCursorShape::hideCursor() {
    g_pSeatManager->m_pPointer->sendSetCursor(lastCursorSerial, nullptr, 0, 0);
}