#include "Screencopy.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../core/hyprlock.hpp"
#include "../core/Egl.hpp"
#include "wlr-screencopy-unstable-v1.hpp"
#include <EGL/eglext.h>
#include <gbm.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <libdrm/drm_fourcc.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC   eglQueryDmaBufModifiersEXT   = nullptr;

//
std::string CDesktopFrame::getResourceId(COutput* output) {
    return std::format("screencopy:{}-{}x{}", output->stringPort, output->size.x, output->size.y);
}

CDesktopFrame::CDesktopFrame(COutput* output) : m_output(output) {
    m_resourceID = getResourceId(m_output);

    captureOutput();
    m_frame = std::make_unique<CSCDMAFrame>(m_sc);
}

void CDesktopFrame::captureOutput() {
    m_sc = makeShared<CCZwlrScreencopyFrameV1>(g_pHyprlock->getScreencopy()->sendCaptureOutput(false, m_output->output->resource()));
    m_sc->setFailed([this](CCZwlrScreencopyFrameV1* r) {
        Debug::log(ERR, "[sc] wlrOnFailed for {}", (void*)r);

        m_frame.reset();

        if (m_dmaFailed) {
            Debug::log(CRIT, "Failed to do screencopy. Didn't work via DMA or SHM");
            return;
        }

        onDmaFailed();
    });

    m_sc->setReady([this](CCZwlrScreencopyFrameV1* r, uint32_t, uint32_t, uint32_t) {
        Debug::log(TRACE, "[sc] wlrOnReady for {}", (void*)this);

        if (!m_frame->m_ok || !m_frame->onBufferReady(m_asset)) {
            Debug::log(ERR, "onBufferReady failed");
            onDmaFailed();
            return;
        }

        m_sc.reset();
    });
}

void CDesktopFrame::onDmaFailed() {
    if (m_dmaFailed)
        return;

    Debug::log(WARN, "Failed to do screencopy via DMA, trying SHM");
    m_dmaFailed = true;
    m_frame.reset();
    m_sc->sendDestroy();
    m_sc.reset();
    captureOutput();
    m_frame = std::make_unique<CSCSHMFrame>(m_sc);
}

CSCDMAFrame::CSCDMAFrame(SP<CCZwlrScreencopyFrameV1> sc) : m_sc(sc) {
    if (!glEGLImageTargetTexture2DOES) {
        glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!glEGLImageTargetTexture2DOES) {
            Debug::log(ERR, "No glEGLImageTargetTexture2DOES??");
            return;
        }
    }

    if (!eglQueryDmaBufModifiersEXT)
        eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");

    m_sc->setBufferDone([this](CCZwlrScreencopyFrameV1* r) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone for {}", (void*)this);

        if (!onBufferDone()) {
            Debug::log(ERR, "onBufferDone failed");
            m_ok = false;
            return;
        }

        m_sc->sendCopy(wlBuffer->resource());

        Debug::log(TRACE, "[sc] wlr frame copied");
    });

    m_sc->setLinuxDmabuf([this](CCZwlrScreencopyFrameV1* r, uint32_t format, uint32_t width, uint32_t height) {
        Debug::log(TRACE, "[sc] wlrOnDmabuf for {}", (void*)this);

        m_w   = width;
        m_h   = height;
        m_fmt = format;

        Debug::log(TRACE, "[sc] DMABUF format reported: {:x}", format);
    });

    m_sc->setBuffer([](CCZwlrScreencopyFrameV1* r, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
        ; // unused by dma
    });
}

CSCDMAFrame::~CSCDMAFrame() {
    if (g_pEGL)
        eglDestroyImage(g_pEGL->eglDisplay, m_image);

    // leaks bo and stuff but lives throughout so for now who cares
}

bool CSCDMAFrame::onBufferDone() {
    uint32_t flags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;

    m_bo = gbm_bo_create(g_pHyprlock->dma.gbmDevice, m_w, m_h, m_fmt, flags);
    if (!m_bo && eglQueryDmaBufModifiersEXT) {
        // try again manually checking for a linear modifier
        // this is how it worked before moving to GL_TEXTURE_EXTERNAL_OES
        Debug::log(WARN, "[bo] gbm_bo_create failed, trying with modifiers");
        std::vector<uint64_t> mods;
        mods.resize(64);
        int num = 0;
        if (eglQueryDmaBufModifiersEXT(g_pEGL->eglDisplay, m_fmt, 64, mods.data(), nullptr, &num) && num > 0) {
            Debug::log(LOG, "[bo] eglQueryDmaBufModifiersEXT found {} mods", num);
            for (int i = 0; i < num; ++i)
                Debug::log(TRACE, "[bo] Modifier {:x}", mods[i]);

            uint64_t zero      = 0;
            bool     hasLinear = std::find(mods.begin(), mods.end(), 0) != mods.end();

            m_bo = gbm_bo_create_with_modifiers2(g_pHyprlock->dma.gbmDevice, m_w, m_h, m_fmt, hasLinear ? &zero : mods.data(), hasLinear ? 1 : mods.size(), GBM_BO_USE_RENDERING);
        } else
            Debug::log(ERR, "[bo] Failed to get modifiers from eglQueryDmaBufModifiersEXT");
    }

    if (!m_bo) {
        Debug::log(ERR, "[bo] Couldn't create a drm buffer");
        return false;
    }

    m_planes = gbm_bo_get_plane_count(m_bo);

    uint64_t mod = gbm_bo_get_modifier(m_bo);
    Debug::log(LOG, "[bo] chose modifier {:x}", mod);

    auto params = makeShared<CCZwpLinuxBufferParamsV1>(g_pHyprlock->dma.linuxDmabuf->sendCreateParams());
    if (!params) {
        Debug::log(ERR, "zwp_linux_dmabuf_v1_create_params failed");
        gbm_bo_destroy(m_bo);
        return false;
    }

    for (size_t plane = 0; plane < (size_t)m_planes; plane++) {
        m_size[plane]   = 0;
        m_stride[plane] = gbm_bo_get_stride_for_plane(m_bo, plane);
        m_offset[plane] = gbm_bo_get_offset(m_bo, plane);
        m_fd[plane]     = gbm_bo_get_fd_for_plane(m_bo, plane);

        if (m_fd[plane] < 0) {
            Debug::log(ERR, "gbm_m_bo_get_fd_for_plane failed");
            params.reset();
            gbm_bo_destroy(m_bo);
            for (size_t plane_tmp = 0; plane_tmp < plane; plane_tmp++) {
                close(m_fd[plane_tmp]);
            }
            return false;
        }

        Debug::log(LOG, "mod upper: {:x}, mod lower: {:x}", mod >> 32, mod & 0xffffffff);
        params->sendAdd(m_fd[plane], plane, m_offset[plane], m_stride[plane], mod >> 32, mod & 0xffffffff);
    }

    wlBuffer = makeShared<CCWlBuffer>(params->sendCreateImmed(m_w, m_h, m_fmt, (zwpLinuxBufferParamsV1Flags)0));
    params.reset();

    if (!wlBuffer) {
        Debug::log(ERR, "[pw] zwp_linux_buffer_params_v1_create_immed failed");
        gbm_bo_destroy(m_bo);
        for (size_t plane = 0; plane < (size_t)m_planes; plane++)
            close(m_fd[plane]);

        return false;
    }

    return true;
}

bool CSCDMAFrame::onBufferReady(SPreloadedAsset& asset) {
    static const int general_attribs    = 3;
    static const int plane_attribs      = 5;
    static const int entries_per_attrib = 2;
    EGLAttrib        attribs[(general_attribs + plane_attribs * 4) * entries_per_attrib + 1];
    int              attr = 0;
    Vector2D         size{m_w, m_h};

    attribs[attr++] = EGL_WIDTH;
    attribs[attr++] = size.x;
    attribs[attr++] = EGL_HEIGHT;
    attribs[attr++] = size.y;
    attribs[attr++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[attr++] = m_fmt;
    attribs[attr++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[attr++] = m_fd[0];
    attribs[attr++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[attr++] = m_offset[0];
    attribs[attr++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[attr++] = m_stride[0];
    attribs[attr]   = EGL_NONE;

    m_image = eglCreateImage(g_pEGL->eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

    if (m_image == EGL_NO_IMAGE) {
        Debug::log(ERR, "failed creating an egl image");
        return false;
    }

    asset.texture.allocate();
    asset.texture.m_vSize = size;
    glBindTexture(GL_TEXTURE_2D, asset.texture.m_iTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
    glBindTexture(GL_TEXTURE_2D, 0);

    Debug::log(LOG, "Got dma frame with size {}", size);

    asset.ready = true;

    return true;
}

CSCSHMFrame::CSCSHMFrame(SP<CCZwlrScreencopyFrameV1> sc) : m_sc(sc) {
    Debug::log(TRACE, "[sc] [shm] Creating a SHM frame");

    m_sc->setBufferDone([this](CCZwlrScreencopyFrameV1* r) {
        Debug::log(TRACE, "[sc] [shm] wlrOnBufferDone for {}", (void*)this);

        m_sc->sendCopy(m_wlBuffer->resource());

        Debug::log(TRACE, "[sc] [shm]  wlr frame copied");
    });

    m_sc->setBuffer([this](CCZwlrScreencopyFrameV1* r, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
        Debug::log(TRACE, "[sc] [shm]  wlrOnBuffer for {}", (void*)this);

        const auto SIZE = stride * height;
        m_shmFmt        = format;
        m_w             = width;
        m_h             = height;
        m_stride        = stride;

        // Create a shm pool with format and size
        std::string shmPoolFile;
        const auto  FD = createPoolFile(SIZE, shmPoolFile);

        if (FD < 0) {
            Debug::log(ERR, "[sc] [shm]  failed to create a pool file");
            return;
        }

        m_shmData = mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, FD, 0);
        if (m_shmData == MAP_FAILED) {
            Debug::log(ERR, "[sc] [shm] failed to (errno {})", strerror(errno));
            close(FD);
            return;
        }

        RASSERT(g_pHyprlock->getShm(), "[sc] [shm] No shm compostor support??");
        auto pShmPool = makeShared<CCWlShmPool>(g_pHyprlock->getShm()->sendCreatePool(FD, SIZE));
        m_wlBuffer    = makeShared<CCWlBuffer>(pShmPool->sendCreateBuffer(0, width, height, stride, m_shmFmt));

        pShmPool.reset();

        close(FD);
    });

    m_sc->setLinuxDmabuf([](CCZwlrScreencopyFrameV1* r, uint32_t, uint32_t, uint32_t) {
        ; // unused by scshm
    });
}

CSCSHMFrame::~CSCSHMFrame() {
    if (m_shmData)
        munmap(m_shmData, m_stride * m_h);
}

void CSCSHMFrame::convertBuffer() {
    const auto BYTESPERPX = m_stride / m_w;
    if (BYTESPERPX == 4) {
        switch (m_shmFmt) {
            case WL_SHM_FORMAT_ARGB8888:
            case WL_SHM_FORMAT_XRGB8888: {
                Debug::log(LOG, "[sc] [shm] Converting ARGB to RGBA");
                uint8_t* data = (uint8_t*)m_shmData;

                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        struct pixel {
                            // little-endian ARGB
                            unsigned char blue;
                            unsigned char green;
                            unsigned char red;
                            unsigned char alpha;
                        }* px = (struct pixel*)(data + y * m_w * 4 + x * 4);

                        // RGBA
                        *px = {px->red, px->green, px->blue, px->alpha};
                    }
                }
            } break;
            case WL_SHM_FORMAT_ABGR8888:
            case WL_SHM_FORMAT_XBGR8888: {
                Debug::log(LOG, "[sc] [shm] Converting ABGR to ARGB");
                uint8_t* data = (uint8_t*)m_shmData;

                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        struct pixel {
                            // little-endian ARGB
                            unsigned char blue;
                            unsigned char green;
                            unsigned char red;
                            unsigned char alpha;
                        }* px = (struct pixel*)(data + y * m_w * 4 + x * 4);

                        // RGBA
                        *px = {px->blue, px->green, px->red, px->alpha};
                    }
                }
            } break;
            case WL_SHM_FORMAT_ABGR2101010:
            case WL_SHM_FORMAT_ARGB2101010:
            case WL_SHM_FORMAT_XRGB2101010:
            case WL_SHM_FORMAT_XBGR2101010: {
                Debug::log(LOG, "[sc] [shm] Converting 10-bit to 8-bit");
                uint8_t*   data = (uint8_t*)m_shmData;

                const bool FLIP = m_shmFmt != WL_SHM_FORMAT_XBGR2101010;

                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        uint32_t* px = (uint32_t*)(data + y * m_w * 4 + x * 4);

                        // conv to 8 bit
                        uint8_t R = (uint8_t)std::round((255.0 * (((*px) & 0b00000000000000000000001111111111) >> 0) / 1023.0));
                        uint8_t G = (uint8_t)std::round((255.0 * (((*px) & 0b00000000000011111111110000000000) >> 10) / 1023.0));
                        uint8_t B = (uint8_t)std::round((255.0 * (((*px) & 0b00111111111100000000000000000000) >> 20) / 1023.0));
                        uint8_t A = (uint8_t)std::round((255.0 * (((*px) & 0b11000000000000000000000000000000) >> 30) / 3.0));

                        // write 8-bit values
                        *px = ((FLIP ? B : R) << 0) + (G << 8) + ((FLIP ? R : B) << 16) + (A << 24);
                    }
                }
            } break;
            default: {
                Debug::log(WARN, "[sc] [shm] Unsupported format {}", m_shmFmt);
            }
        }
    } else {
        Debug::log(CRIT, "[sc] [shm] Unsupported bytes per pixel {}", BYTESPERPX);
    }
}

bool CSCSHMFrame::onBufferReady(SPreloadedAsset& asset) {
    convertBuffer();

    asset.texture.allocate();
    asset.texture.m_vSize.x = m_w;
    asset.texture.m_vSize.y = m_h;

    glBindTexture(GL_TEXTURE_2D, asset.texture.m_iTexID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_w, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_shmData);
    glBindTexture(GL_TEXTURE_2D, 0);

    Debug::log(LOG, "[sc] [shm] Got screenshot with size {}", asset.texture.m_vSize);

    asset.ready = true;

    return true;
}
