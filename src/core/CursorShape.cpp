#include "CursorShape.hpp"
#include "hyprlock.hpp"

CCursorShape::CCursorShape(wp_cursor_shape_manager_v1* mgr) : mgr(mgr) {
    if (!g_pHyprlock->m_pPointer)
        return;

    dev = wp_cursor_shape_manager_v1_get_pointer(mgr, g_pHyprlock->m_pPointer);
}

void CCursorShape::setShape(const uint32_t serial, const wp_cursor_shape_device_v1_shape shape) {
    if (!dev)
        return;

    wp_cursor_shape_device_v1_set_shape(dev, serial, shape);
}

void CCursorShape::hideCursor(const uint32_t serial) {
    wl_pointer_set_cursor(g_pHyprlock->m_pPointer, serial, nullptr, 0, 0);
}