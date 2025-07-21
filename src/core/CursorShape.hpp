#pragma once

#include "../defines.hpp"
#include "cursor-shape-v1.hpp"

class CCursorShape {
  public:
    CCursorShape(SP<CCWpCursorShapeManagerV1> mgr);

    void     setShape(const wpCursorShapeDeviceV1Shape shape);
    void     hideCursor();

    uint32_t lastCursorSerial = 0;
    bool     shapeChanged     = false;

  private:
    SP<CCWpCursorShapeManagerV1> mgr = nullptr;
    SP<CCWpCursorShapeDeviceV1>  dev = nullptr;
};
