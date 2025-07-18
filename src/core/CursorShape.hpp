#pragma once

#include "../defines.hpp"
#include "cursor-shape-v1.hpp"

class CCursorShape {
  public:
    CCursorShape(SP<CCWpCursorShapeManagerV1> mgr);

    void     setShape(const wpCursorShapeDeviceV1Shape shape);
    void     hideCursor(wl_proxy* surf, wl_fixed_t surface_x, wl_fixed_t surface_y);
    void     restoreCursor();

    uint32_t lastCursorSerial = 0;

  private:
    SP<CCWpCursorShapeManagerV1> mgr = nullptr;
    SP<CCWpCursorShapeDeviceV1>  dev = nullptr;

    bool                         m_hidden = false;
    struct {
        wl_proxy*  surf      = nullptr; // Never dereference! Only for restoring the cursor
        wl_fixed_t surface_x = 0;
        wl_fixed_t surface_y = 0;
    } m_cursorStateBackup;
};
