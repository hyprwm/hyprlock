#pragma once

#include "../defines.hpp"
#include "../core/Output.hpp"
#include <cstdint>
#include <gbm.h>
#include <memory>
#include "Shared.hpp"
#include "linux-dmabuf-v1.hpp"
#include "wlr-screencopy-unstable-v1.hpp"

class ISCFrame {
  public:
    ISCFrame()          = default;
    virtual ~ISCFrame() = default;

    virtual bool onBufferReady(SPreloadedAsset& asset) = 0;

    bool         m_ok = true;
};

class CDesktopFrame {
  public:
    static std::string getResourceId(COutput* output);

    CDesktopFrame(COutput* mon);
    ~CDesktopFrame() = default;

    void                        captureOutput();
    void                        onDmaFailed();

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    std::string                 m_resourceID;
    SPreloadedAsset             m_asset;

  private:
    COutput*                  m_output = nullptr;
    std::unique_ptr<ISCFrame> m_frame  = nullptr;

    bool                      m_dmaFailed = false;
};

class CSCDMAFrame : public ISCFrame {
  public:
    CSCDMAFrame(SP<CCZwlrScreencopyFrameV1> sc);
    virtual ~CSCDMAFrame();

    virtual bool   onBufferReady(SPreloadedAsset& asset);
    bool           onBufferDone();

    SP<CCWlBuffer> wlBuffer = nullptr;

  private:
    gbm_bo*                     m_bo = nullptr;

    int                         m_planes = 0;

    int                         m_fd[4];
    uint32_t                    m_size[4], m_stride[4], m_offset[4];

    int                         m_w = 0, m_h = 0;
    uint32_t                    m_fmt = 0;

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    EGLImage                    m_image = nullptr;
};

// CPU based fallback if DMA failes
class CSCSHMFrame : public ISCFrame {
  public:
    CSCSHMFrame(SP<CCZwlrScreencopyFrameV1> sc);
    virtual ~CSCSHMFrame();

    virtual bool   onBufferReady(SPreloadedAsset& asset);
    void           convertBuffer();

    SP<CCWlBuffer> m_wlBuffer = nullptr;

  private:
    uint32_t                    m_w = 0, m_h = 0;
    uint32_t                    m_stride = 0;

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    uint32_t                    m_shmFmt  = 0;
    void*                       m_shmData = nullptr;
};
