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

    virtual bool   onBufferDone()                        = 0;
    virtual bool   onBufferReady(SPreloadedAsset& asset) = 0;

    SP<CCWlBuffer> m_wlBuffer = nullptr;
};

class CScreencopyFrame {
  public:
    static std::string getResourceId(COutput* output);

    CScreencopyFrame(COutput* mon);
    ~CScreencopyFrame() = default;

    void                        captureOutput();

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    std::string                 m_resourceID;
    SPreloadedAsset             m_asset;

  private:
    COutput*                  m_output = nullptr;
    std::unique_ptr<ISCFrame> m_frame  = nullptr;

    bool                      m_dmaFailed = false;
};

// Uses a gpu buffer created via gbm_bo
class CSCDMAFrame : public ISCFrame {
  public:
    CSCDMAFrame(SP<CCZwlrScreencopyFrameV1> sc);
    virtual ~CSCDMAFrame();

    virtual bool onBufferReady(SPreloadedAsset& asset);
    virtual bool onBufferDone();

  private:
    gbm_bo*                     m_bo = nullptr;

    int                         m_planes = 0;
    uint64_t                    m_mod    = 0;

    int                         m_fd[4];
    uint32_t                    m_stride[4], m_offset[4];

    int                         m_w = 0, m_h = 0;
    uint32_t                    m_fmt = 0;

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    EGLImage                    m_image = nullptr;
};

// Uses a shm buffer - is slow and needs ugly format conversion
// Used as a fallback just in case.
class CSCSHMFrame : public ISCFrame {
  public:
    CSCSHMFrame(SP<CCZwlrScreencopyFrameV1> sc);
    virtual ~CSCSHMFrame();

    virtual bool onBufferDone() {
        return m_ok;
    }
    virtual bool onBufferReady(SPreloadedAsset& asset);
    void         convertBuffer();

  private:
    bool                        m_ok = true;

    uint32_t                    m_w = 0, m_h = 0;
    uint32_t                    m_stride = 0;

    SP<CCZwlrScreencopyFrameV1> m_sc = nullptr;

    uint32_t                    m_shmFmt     = 0;
    void*                       m_shmData    = nullptr;
    void*                       m_convBuffer = nullptr;
};
