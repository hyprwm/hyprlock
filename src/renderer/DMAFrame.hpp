#pragma once

#include "../defines.hpp"
#include "../core/Output.hpp"
#include <gbm.h>
#include "Shared.hpp"
#include "linux-dmabuf-v1.hpp"
#include "wlr-screencopy-unstable-v1.hpp"

class CDMAFrame;

class CDMAFrame {
  public:
    static std::string getResourceId(COutput* output);

    CDMAFrame(COutput* mon);
    ~CDMAFrame();

    bool            onBufferDone();
    bool            onBufferReady();

    SP<CCWlBuffer>  wlBuffer = nullptr;

    std::string     resourceID;

    SPreloadedAsset asset;

  private:
    gbm_bo*                     bo = nullptr;

    int                         planes = 0;

    int                         fd[4];
    uint32_t                    size[4], stride[4], offset[4];

    SP<CCZwlrScreencopyFrameV1> frameCb = nullptr;
    int                         w = 0, h = 0;
    uint32_t                    fmt         = 0;
    size_t                      frameSize   = 0;
    size_t                      frameStride = 0;

    EGLImage                    image = nullptr;
};