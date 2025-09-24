#pragma once

#include "../defines.hpp"
#include "wayland.hpp"
#include "LockSurface.hpp"

class COutput {
  public:
    COutput()  = default;
    ~COutput() = default;

    void                    create(WP<COutput> pSelf, SP<CCWlOutput> pWlOutput, uint32_t name);

    OUTPUTID                m_ID      = 0;
    bool                    focused   = false;
    bool                    done      = false;
    wl_output_transform     transform = WL_OUTPUT_TRANSFORM_NORMAL;
    Vector2D                size;
    int                     scale      = 1;
    std::string             stringName = "";
    std::string             stringPort = "";
    std::string             stringDesc = "";

    UP<CSessionLockSurface> m_sessionLockSurface;

    SP<CCWlOutput>          m_wlOutput = nullptr;

    WP<COutput>             m_self;

    void                    createSessionLockSurface();

    Vector2D                getViewport() const;
    size_t                  getScreencopyResourceID() const;
};
