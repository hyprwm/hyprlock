#include "Screencopy.hpp"
#include "./AsyncResourceManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../core/hyprlock.hpp"
#include "../core/Egl.hpp"
#include "../config/ConfigManager.hpp"
#include "wlr-screencopy-unstable-v1.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstring>
#include <array>
#include <cstdint>
#include <gbm.h>
#include <hyprutils/memory/UniquePtr.hpp>
#include <unistd.h>
#include <sys/mman.h>
#include <libdrm/drm_fourcc.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC   eglQueryDmaBufModifiersEXT   = nullptr;

//
void CScreencopyFrame::capture(SP<COutput> pOutput) {
    RASSERT(pOutput, "Screencopy, but no valid output");

    static const auto SCMODE = g_pConfigManager->getValue<Hyprlang::INT>("general:screencopy_mode");

    m_asset      = makeAtomicShared<CTexture>();
    m_resourceID = CAsyncResourceManager::resourceIDForScreencopy(pOutput->stringPort);

    m_sc = makeShared<CCZwlrScreencopyFrameV1>(g_pHyprlock->getScreencopy()->sendCaptureOutput(false, pOutput->m_wlOutput->resource()));

    if (*SCMODE == 1)
        m_frame = makeUnique<CSCSHMFrame>(m_sc);
    else
        m_frame = makeUnique<CSCDMAFrame>(m_sc);

    m_sc->setBufferDone([this](CCZwlrScreencopyFrameV1* r) {
        Debug::log(TRACE, "[sc] wlrOnBufferDone for {}", (void*)this);

        if (!m_frame || !m_frame->onBufferDone() || !m_frame->m_wlBuffer) {
            Debug::log(ERR, "[sc] Failed to create a wayland buffer for the screencopy frame");
            return;
        }

        m_sc->sendCopy(m_frame->m_wlBuffer->resource());

        Debug::log(TRACE, "[sc] wlr frame copied");
    });

    m_sc->setFailed([this](CCZwlrScreencopyFrameV1* r) {
        Debug::log(ERR, "[sc] wlrOnFailed for {}", (void*)r);

        m_frame.reset();
    });

    m_sc->setReady([this](CCZwlrScreencopyFrameV1* r, uint32_t, uint32_t, uint32_t) {
        Debug::log(TRACE, "[sc] wlrOnReady for {}", (void*)this);

        if (!m_frame || !m_frame->onBufferReady(m_asset)) {
            Debug::log(ERR, "[sc] Failed to bind the screencopy buffer to a texture");
            return;
        }

        m_sc.reset();
        m_ready = true;
        g_asyncResourceManager->screencopyToTexture(*this);
    });
}

CSCDMAFrame::CSCDMAFrame(SP<CCZwlrScreencopyFrameV1> sc) : m_sc(sc) {
    if (!glEGLImageTargetTexture2DOES) {
        glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!glEGLImageTargetTexture2DOES) {
            Debug::log(ERR, "[sc] No glEGLImageTargetTexture2DOES??");
            return;
        }
    }

    if (!g_pHyprlock->dma.linuxDmabuf) {
        Debug::log(ERR, "[sc] No DMABUF support?");
        return;
    }

    if (!g_pHyprlock->dma.gbmDevice) {
        Debug::log(ERR, "[sc] No gbmDevice for DMABUF was created?");
        return;
    }

    if (!eglQueryDmaBufModifiersEXT)
        eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");

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
    uint32_t flags = GBM_BO_USE_RENDERING;

    if (!eglQueryDmaBufModifiersEXT) {
        Debug::log(WARN, "Querying modifiers without eglQueryDmaBufModifiersEXT support");
        m_bo = gbm_bo_create(g_pHyprlock->dma.gbmDevice, m_w, m_h, m_fmt, flags);
    } else {
        std::array<uint64_t, 64>   mods;
        std::array<EGLBoolean, 64> externalOnly;
        int                        num = 0;
        if (!eglQueryDmaBufModifiersEXT(g_pEGL->eglDisplay, m_fmt, 64, mods.data(), externalOnly.data(), &num) || num == 0) {
            Debug::log(WARN, "eglQueryDmaBufModifiersEXT failed, falling back to regular bo");
            m_bo = gbm_bo_create(g_pHyprlock->dma.gbmDevice, m_w, m_h, m_fmt, flags);
        } else {
            Debug::log(LOG, "eglQueryDmaBufModifiersEXT found {} mods", num);
            std::vector<uint64_t> goodMods;
            for (int i = 0; i < num; ++i) {
                if (externalOnly[i]) {
                    Debug::log(TRACE, "Modifier {:x} failed test", mods[i]);
                    continue;
                }

                Debug::log(TRACE, "Modifier {:x} passed test", mods[i]);
                goodMods.emplace_back(mods[i]);
            }

            m_bo = gbm_bo_create_with_modifiers2(g_pHyprlock->dma.gbmDevice, m_w, m_h, m_fmt, goodMods.data(), goodMods.size(), flags);
        }
    }

    if (!m_bo) {
        Debug::log(ERR, "[bo] Couldn't create a drm buffer");
        return false;
    }

    m_planes = gbm_bo_get_plane_count(m_bo);
    Debug::log(LOG, "[bo] has {} plane(s)", m_planes);

    m_mod = gbm_bo_get_modifier(m_bo);
    Debug::log(LOG, "[bo] chose modifier {:x}", m_mod);

    auto params = makeShared<CCZwpLinuxBufferParamsV1>(g_pHyprlock->dma.linuxDmabuf->sendCreateParams());
    if (!params) {
        Debug::log(ERR, "zwp_linux_dmabuf_v1_create_params failed");
        gbm_bo_destroy(m_bo);
        return false;
    }

    for (size_t plane = 0; plane < (size_t)m_planes; plane++) {
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

        params->sendAdd(m_fd[plane], plane, m_offset[plane], m_stride[plane], m_mod >> 32, m_mod & 0xffffffff);
    }

    m_wlBuffer = makeShared<CCWlBuffer>(params->sendCreateImmed(m_w, m_h, m_fmt, (zwpLinuxBufferParamsV1Flags)0));
    params.reset();

    if (!m_wlBuffer) {
        Debug::log(ERR, "[pw] zwp_linux_buffer_params_v1_create_immed failed");
        gbm_bo_destroy(m_bo);
        for (size_t plane = 0; plane < (size_t)m_planes; plane++)
            close(m_fd[plane]);

        return false;
    }

    return true;
}

bool CSCDMAFrame::onBufferReady(ASP<CTexture> texture) {
    static constexpr struct {
        EGLAttrib fd;
        EGLAttrib offset;
        EGLAttrib pitch;
        EGLAttrib modlo;
        EGLAttrib modhi;
    } attrNames[4] = {{.fd     = EGL_DMA_BUF_PLANE0_FD_EXT,
                       .offset = EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                       .pitch  = EGL_DMA_BUF_PLANE0_PITCH_EXT,
                       .modlo  = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                       .modhi  = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT},
                      {.fd     = EGL_DMA_BUF_PLANE1_FD_EXT,
                       .offset = EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                       .pitch  = EGL_DMA_BUF_PLANE1_PITCH_EXT,
                       .modlo  = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                       .modhi  = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT},
                      {.fd     = EGL_DMA_BUF_PLANE2_FD_EXT,
                       .offset = EGL_DMA_BUF_PLANE2_OFFSET_EXT,
                       .pitch  = EGL_DMA_BUF_PLANE2_PITCH_EXT,
                       .modlo  = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
                       .modhi  = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT},
                      {.fd     = EGL_DMA_BUF_PLANE3_FD_EXT,
                       .offset = EGL_DMA_BUF_PLANE3_OFFSET_EXT,
                       .pitch  = EGL_DMA_BUF_PLANE3_PITCH_EXT,
                       .modlo  = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
                       .modhi  = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT}};

    std::vector<EGLAttrib> attribs = {
        EGL_WIDTH, m_w, EGL_HEIGHT, m_h, EGL_LINUX_DRM_FOURCC_EXT, m_fmt,
    };
    for (int i = 0; i < m_planes; i++) {
        attribs.emplace_back(attrNames[i].fd);
        attribs.emplace_back(m_fd[i]);
        attribs.emplace_back(attrNames[i].offset);
        attribs.emplace_back(m_offset[i]);
        attribs.emplace_back(attrNames[i].pitch);
        attribs.emplace_back(m_stride[i]);
        if (m_mod != DRM_FORMAT_MOD_INVALID) {
            attribs.emplace_back(attrNames[i].modlo);
            attribs.emplace_back(m_mod & 0xFFFFFFFF);
            attribs.emplace_back(attrNames[i].modhi);
            attribs.emplace_back(m_mod >> 32);
        }
    }
    attribs.emplace_back(EGL_NONE);

    m_image = eglCreateImage(g_pEGL->eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs.data());

    if (m_image == EGL_NO_IMAGE) {
        Debug::log(ERR, "Failed creating an egl image");
        return false;
    }

    texture->allocate();
    texture->m_vSize = {m_w, m_h};
    glBindTexture(GL_TEXTURE_2D, texture->m_iTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
    glBindTexture(GL_TEXTURE_2D, 0);

    Debug::log(LOG, "Got dma frame with size {}", texture->m_vSize);

    return true;
}

CSCSHMFrame::CSCSHMFrame(SP<CCZwlrScreencopyFrameV1> sc) : m_sc(sc) {
    Debug::log(TRACE, "[sc] [shm] Creating a SHM frame");

    m_sc->setBuffer([this](CCZwlrScreencopyFrameV1* r, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
        Debug::log(TRACE, "[sc] [shm] wlrOnBuffer for {}", (void*)this);

        const auto SIZE = stride * height;
        m_shmFmt        = format;
        m_w             = width;
        m_h             = height;
        m_stride        = stride;

        // Create a shm pool with format and size
        std::string shmPoolFile;
        const auto  FD = createPoolFile(SIZE, shmPoolFile);

        if (FD < 0) {
            Debug::log(ERR, "[sc] [shm] failed to create a pool file");
            return;
        }

        m_shmData = mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, FD, 0);
        if (m_shmData == MAP_FAILED) {
            Debug::log(ERR, "[sc] [shm] failed to (errno {})", strerror(errno));
            close(FD);
            m_ok = false;
            return;
        }

        if (!g_pHyprlock->getShm()) {
            Debug::log(ERR, "[sc] [shm] Failed to get WLShm global");
            close(FD);
            m_ok = false;
            return;
        }

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
    if (m_convBuffer)
        free(m_convBuffer);
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
                        }* px = (struct pixel*)(data + (y * m_w * 4) + (x * 4));

                        // RGBA
                        *px = {.blue = px->red, .green = px->green, .red = px->blue, .alpha = px->alpha};
                    }
                }
            } break;
            case WL_SHM_FORMAT_ABGR8888:
            case WL_SHM_FORMAT_XBGR8888: {
                Debug::log(LOG, "[sc] [shm] Converting ABGR to RGBA");
                uint8_t* data = (uint8_t*)m_shmData;

                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        struct pixel {
                            // little-endian ARGB
                            unsigned char blue;
                            unsigned char green;
                            unsigned char red;
                            unsigned char alpha;
                        }* px = (struct pixel*)(data + (y * m_w * 4) + (x * 4));

                        // BGRA
                        *px = {.blue = px->blue, .green = px->green, .red = px->red, .alpha = px->alpha};
                    }
                }
            } break;
            case WL_SHM_FORMAT_ABGR2101010:
            case WL_SHM_FORMAT_ARGB2101010:
            case WL_SHM_FORMAT_XRGB2101010:
            case WL_SHM_FORMAT_XBGR2101010: {
                Debug::log(LOG, "[sc] [shm] Converting 10-bit channels to 8-bit");
                uint8_t*   data = (uint8_t*)m_shmData;

                const bool FLIP = m_shmFmt != WL_SHM_FORMAT_XBGR2101010;

                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        uint32_t* px = (uint32_t*)(data + (y * m_w * 4) + (x * 4));

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
    } else if (BYTESPERPX == 3) {
        Debug::log(LOG, "[sc] [shm] Converting 24 bit to 32 bit");
        m_convBuffer        = malloc(m_w * m_h * 4);
        const int NEWSTRIDE = m_w * 4;
        RASSERT(m_convBuffer, "malloc failed");

        switch (m_shmFmt) {
            case WL_SHM_FORMAT_BGR888: {
                Debug::log(LOG, "[sc] [shm] Converting BGR to RGBA");
                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        struct pixel3 {
                            // little-endian RGB
                            unsigned char blue;
                            unsigned char green;
                            unsigned char red;
                        }* srcPx = (struct pixel3*)((char*)m_shmData + (y * m_stride) + (x * 3));
                        struct pixel4 {
                            // little-endian ARGB
                            unsigned char blue;
                            unsigned char green;
                            unsigned char red;
                            unsigned char alpha;
                        }* dstPx = (struct pixel4*)((char*)m_convBuffer + (y * NEWSTRIDE) + (x * 4));
                        *dstPx   = {.blue = srcPx->blue, .green = srcPx->green, .red = srcPx->red, .alpha = 0xFF};
                    }
                }
            } break;
            case WL_SHM_FORMAT_RGB888: {
                Debug::log(LOG, "[sc] [shm] Converting RGB to RGBA");
                for (uint32_t y = 0; y < m_h; ++y) {
                    for (uint32_t x = 0; x < m_w; ++x) {
                        struct pixel3 {
                            // big-endian RGB
                            unsigned char red;
                            unsigned char green;
                            unsigned char blue;
                        }* srcPx = (struct pixel3*)((char*)m_shmData + (y * m_stride) + (x * 3));
                        struct pixel4 {
                            // big-endian ARGB
                            unsigned char alpha;
                            unsigned char red;
                            unsigned char green;
                            unsigned char blue;
                        }* dstPx = (struct pixel4*)((char*)m_convBuffer + (y * NEWSTRIDE) + (x * 4));
                        *dstPx   = {.alpha = srcPx->red, .red = srcPx->green, .green = srcPx->blue, .blue = 0xFF};
                    }
                }
            } break;
            default: {
                Debug::log(ERR, "[sc] [shm] Unsupported format for 24bit buffer {}", m_shmFmt);
            }
        }

    } else {
        Debug::log(ERR, "[sc] [shm] Unsupported bytes per pixel {}", BYTESPERPX);
    }
}

bool CSCSHMFrame::onBufferReady(ASP<CTexture> texture) {
    convertBuffer();

    texture->allocate();
    texture->m_vSize.x = m_w;
    texture->m_vSize.y = m_h;

    glBindTexture(GL_TEXTURE_2D, texture->m_iTexID);

    void* buffer = m_convBuffer ? m_convBuffer : m_shmData;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_w, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    glBindTexture(GL_TEXTURE_2D, 0);

    Debug::log(LOG, "[sc] [shm] Got screenshot with size {}", texture->m_vSize);

    return true;
}
