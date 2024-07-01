#pragma once

#include "../core/Output.hpp"
#include <gbm.h>
#include "Texture.hpp"
#include "Shared.hpp"

struct zwlr_screencopy_frame_v1;

class CDMAFrame;

struct SScreencopyData {
    int        w = 0, h = 0;
    uint32_t   fmt;
    size_t     size;
    size_t     stride;
    CDMAFrame* frame = nullptr;
};

class CDMAFrame {
  public:
    static std::string getResourceId(COutput* output);

    CDMAFrame(COutput* mon);
    ~CDMAFrame();

    bool            onBufferDone();
    bool            onBufferReady();

    wl_buffer*      wlBuffer = nullptr;

    std::string     resourceID;

    SPreloadedAsset asset;

  private:
    gbm_bo*                   bo = nullptr;

    int                       planes = 0;

    int                       fd[4];
    uint32_t                  size[4], stride[4], offset[4];

    zwlr_screencopy_frame_v1* frameCb = nullptr;
    SScreencopyData           scdata;

    EGLImage                  image = nullptr;
};