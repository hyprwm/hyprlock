#pragma once

#include <wayland-client.h>
#include "../helpers/Math.hpp"
#include "LockSurface.hpp"
#include <memory>

class COutput {
  public:
    COutput(wl_output* output, uint32_t name);

    uint32_t                             name      = 0;
    bool                                 focused   = false;
    wl_output_transform                  transform = WL_OUTPUT_TRANSFORM_NORMAL;
    Vector2D                             size;
    int                                  scale      = 1;
    std::string                          stringName = "";
    std::string                          stringPort = "";
    std::string                          stringDesc = "";

    std::unique_ptr<CSessionLockSurface> sessionLockSurface;

    wl_output*                           output = nullptr;

  private:
};
